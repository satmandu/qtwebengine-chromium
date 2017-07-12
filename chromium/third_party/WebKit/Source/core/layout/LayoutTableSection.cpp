/*
 * Copyright (C) 1997 Martin Jones (mjones@kde.org)
 *           (C) 1997 Torben Weis (weis@kde.org)
 *           (C) 1998 Waldo Bastian (bastian@kde.org)
 *           (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 * Copyright (C) 2003, 2004, 2005, 2006, 2008, 2009, 2010, 2013 Apple Inc.
 *               All rights reserved.
 * Copyright (C) 2006 Alexey Proskuryakov (ap@nypop.com)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "core/layout/LayoutTableSection.h"

#include <algorithm>
#include <limits>
#include "core/layout/HitTestResult.h"
#include "core/layout/LayoutAnalyzer.h"
#include "core/layout/LayoutTableCell.h"
#include "core/layout/LayoutTableCol.h"
#include "core/layout/LayoutTableRow.h"
#include "core/layout/LayoutView.h"
#include "core/layout/SubtreeLayoutScope.h"
#include "core/paint/TableSectionPainter.h"
#include "platform/wtf/HashSet.h"

namespace blink {

using namespace HTMLNames;

// This variable is used to balance the memory consumption vs the paint
// invalidation time on big tables.
static unsigned g_min_table_size_to_use_fast_paint_path_with_overflowing_cell =
    75 * 75;

static inline void SetRowLogicalHeightToRowStyleLogicalHeight(
    LayoutTableSection::RowStruct& row) {
  DCHECK(row.row_layout_object);
  row.logical_height = row.row_layout_object->Style()->LogicalHeight();
}

static inline void UpdateLogicalHeightForCell(
    LayoutTableSection::RowStruct& row,
    const LayoutTableCell* cell) {
  // We ignore height settings on rowspan cells.
  if (cell->RowSpan() != 1)
    return;

  Length logical_height = cell->Style()->LogicalHeight();
  if (logical_height.IsPositive()) {
    Length c_row_logical_height = row.logical_height;
    switch (logical_height.GetType()) {
      case kPercent:
        // TODO(alancutter): Make this work correctly for calc lengths.
        if (!(c_row_logical_height.IsPercentOrCalc()) ||
            (c_row_logical_height.IsPercent() &&
             c_row_logical_height.Percent() < logical_height.Percent()))
          row.logical_height = logical_height;
        break;
      case kFixed:
        if (c_row_logical_height.GetType() < kPercent ||
            (c_row_logical_height.IsFixed() &&
             c_row_logical_height.Value() < logical_height.Value()))
          row.logical_height = logical_height;
        break;
      default:
        break;
    }
  }
}

void CellSpan::EnsureConsistency(const unsigned maximum_span_size) {
  static_assert(std::is_same<decltype(start_), unsigned>::value,
                "Asserts below assume m_start is unsigned");
  static_assert(std::is_same<decltype(end_), unsigned>::value,
                "Asserts below assume m_end is unsigned");
  CHECK_LE(start_, maximum_span_size);
  CHECK_LE(end_, maximum_span_size);
  CHECK_LE(start_, end_);
}

LayoutTableSection::CellStruct::CellStruct() : in_col_span(false) {}

LayoutTableSection::CellStruct::~CellStruct() {}

LayoutTableSection::LayoutTableSection(Element* element)
    : LayoutTableBoxComponent(element),
      c_col_(0),
      c_row_(0),
      outer_border_start_(0),
      outer_border_end_(0),
      outer_border_before_(0),
      outer_border_after_(0),
      needs_cell_recalc_(false),
      force_slow_paint_path_with_overflowing_cell_(false),
      has_multiple_cell_levels_(false),
      has_spanning_cells_(false) {
  // init LayoutObject attributes
  SetInline(false);  // our object is not Inline
}

LayoutTableSection::~LayoutTableSection() {}

void LayoutTableSection::StyleDidChange(StyleDifference diff,
                                        const ComputedStyle* old_style) {
  DCHECK(Style()->Display() == EDisplay::kTableFooterGroup ||
         Style()->Display() == EDisplay::kTableRowGroup ||
         Style()->Display() == EDisplay::kTableHeaderGroup);

  LayoutTableBoxComponent::StyleDidChange(diff, old_style);
  PropagateStyleToAnonymousChildren();

  if (!old_style)
    return;

  LayoutTable* table = this->Table();
  if (!table)
    return;

  if (!table->SelfNeedsLayout() && !table->NormalChildNeedsLayout() &&
      old_style->Border() != Style()->Border())
    table->InvalidateCollapsedBorders();

  if (LayoutTableBoxComponent::DoCellsHaveDirtyWidth(*this, *table, diff,
                                                     *old_style))
    MarkAllCellsWidthsDirtyAndOrNeedsLayout(
        LayoutTable::kMarkDirtyAndNeedsLayout);
}

void LayoutTableSection::WillBeRemovedFromTree() {
  LayoutTableBoxComponent::WillBeRemovedFromTree();

  // Preventively invalidate our cells as we may be re-inserted into
  // a new table which would require us to rebuild our structure.
  SetNeedsCellRecalc();
}

void LayoutTableSection::AddChild(LayoutObject* child,
                                  LayoutObject* before_child) {
  if (!child->IsTableRow()) {
    LayoutObject* last = before_child;
    if (!last)
      last = LastRow();
    if (last && last->IsAnonymous() && !last->IsBeforeOrAfterContent()) {
      if (before_child == last)
        before_child = last->SlowFirstChild();
      last->AddChild(child, before_child);
      return;
    }

    if (before_child && !before_child->IsAnonymous() &&
        before_child->Parent() == this) {
      LayoutObject* row = before_child->PreviousSibling();
      if (row && row->IsTableRow() && row->IsAnonymous()) {
        row->AddChild(child);
        return;
      }
    }

    // If beforeChild is inside an anonymous cell/row, insert into the cell or
    // into the anonymous row containing it, if there is one.
    LayoutObject* last_box = last;
    while (last_box && last_box->Parent()->IsAnonymous() &&
           !last_box->IsTableRow())
      last_box = last_box->Parent();
    if (last_box && last_box->IsAnonymous() &&
        !last_box->IsBeforeOrAfterContent()) {
      last_box->AddChild(child, before_child);
      return;
    }

    LayoutObject* row = LayoutTableRow::CreateAnonymousWithParent(this);
    AddChild(row, before_child);
    row->AddChild(child);
    return;
  }

  if (before_child)
    SetNeedsCellRecalc();

  unsigned insertion_row = c_row_;
  ++c_row_;
  c_col_ = 0;

  EnsureRows(c_row_);

  LayoutTableRow* row = ToLayoutTableRow(child);
  grid_[insertion_row].row_layout_object = row;
  row->SetRowIndex(insertion_row);

  if (!before_child)
    SetRowLogicalHeightToRowStyleLogicalHeight(grid_[insertion_row]);

  if (before_child && before_child->Parent() != this)
    before_child = SplitAnonymousBoxesAroundChild(before_child);

  DCHECK(!before_child || before_child->IsTableRow());
  LayoutTableBoxComponent::AddChild(child, before_child);
}

static inline void CheckThatVectorIsDOMOrdered(
    const Vector<LayoutTableCell*, 1>& cells) {
#ifndef NDEBUG
  // This function should be called on a non-empty vector.
  DCHECK_GT(cells.size(), 0u);

  const LayoutTableCell* previous_cell = cells[0];
  for (size_t i = 1; i < cells.size(); ++i) {
    const LayoutTableCell* current_cell = cells[i];
    // The check assumes that all cells belong to the same row group.
    DCHECK_EQ(previous_cell->Section(), current_cell->Section());

    // 2 overlapping cells can't be on the same row.
    DCHECK_NE(current_cell->Row(), previous_cell->Row());

    // Look backwards in the tree for the previousCell's row. If we are
    // DOM ordered, we should find it.
    const LayoutTableRow* row = current_cell->Row();
    for (; row && row != previous_cell->Row(); row = row->PreviousRow()) {
    }
    DCHECK_EQ(row, previous_cell->Row());

    previous_cell = current_cell;
  }
#endif  // NDEBUG
}

void LayoutTableSection::AddCell(LayoutTableCell* cell, LayoutTableRow* row) {
  // We don't insert the cell if we need cell recalc as our internal columns'
  // representation will have drifted from the table's representation. Also
  // recalcCells will call addCell at a later time after sync'ing our columns'
  // with the table's.
  if (NeedsCellRecalc())
    return;

  unsigned r_span = cell->RowSpan();
  unsigned c_span = cell->ColSpan();
  if (r_span > 1 || c_span > 1)
    has_spanning_cells_ = true;

  const Vector<LayoutTable::ColumnStruct>& columns =
      Table()->EffectiveColumns();
  unsigned insertion_row = row->RowIndex();

  // ### mozilla still seems to do the old HTML way, even for strict DTD
  // (see the annotation on table cell layouting in the CSS specs and the
  // testcase below:
  // <TABLE border>
  // <TR><TD>1 <TD rowspan="2">2 <TD>3 <TD>4
  // <TR><TD colspan="2">5
  // </TABLE>
  unsigned n_cols = NumCols(insertion_row);
  while (c_col_ < n_cols && (CellAt(insertion_row, c_col_).HasCells() ||
                             CellAt(insertion_row, c_col_).in_col_span))
    c_col_++;

  UpdateLogicalHeightForCell(grid_[insertion_row], cell);

  EnsureRows(insertion_row + r_span);

  grid_[insertion_row].row_layout_object = row;

  unsigned col = c_col_;
  // tell the cell where it is
  bool in_col_span = false;
  unsigned col_size = columns.size();
  while (c_span) {
    unsigned current_span;
    if (c_col_ >= col_size) {
      Table()->AppendEffectiveColumn(c_span);
      current_span = c_span;
    } else {
      if (c_span < columns[c_col_].span)
        Table()->SplitEffectiveColumn(c_col_, c_span);
      current_span = columns[c_col_].span;
    }
    for (unsigned r = 0; r < r_span; r++) {
      EnsureCols(insertion_row + r, c_col_ + 1);
      CellStruct& c = CellAt(insertion_row + r, c_col_);
      DCHECK(cell);
      c.cells.push_back(cell);
      CheckThatVectorIsDOMOrdered(c.cells);
      // If cells overlap then we take the slow path for painting.
      if (c.cells.size() > 1)
        has_multiple_cell_levels_ = true;
      if (in_col_span)
        c.in_col_span = true;
    }
    c_col_++;
    c_span -= current_span;
    in_col_span = true;
  }
  cell->SetAbsoluteColumnIndex(Table()->EffectiveColumnToAbsoluteColumn(col));
}

bool LayoutTableSection::RowHasOnlySpanningCells(unsigned row) {
  unsigned total_cols = grid_[row].row.size();

  if (!total_cols)
    return false;

  for (unsigned col = 0; col < total_cols; col++) {
    const CellStruct& row_span_cell = CellAt(row, col);

    // Empty cell is not a valid cell so it is not a rowspan cell.
    if (row_span_cell.cells.IsEmpty())
      return false;

    if (row_span_cell.cells[0]->RowSpan() == 1)
      return false;
  }

  return true;
}

void LayoutTableSection::PopulateSpanningRowsHeightFromCell(
    LayoutTableCell* cell,
    struct SpanningRowsHeight& spanning_rows_height) {
  const unsigned row_span = cell->RowSpan();
  const unsigned row_index = cell->RowIndex();

  spanning_rows_height.spanning_cell_height_ignoring_border_spacing =
      cell->LogicalHeightForRowSizing();

  spanning_rows_height.row_height.Resize(row_span);
  spanning_rows_height.total_rows_height = 0;
  for (unsigned row = 0; row < row_span; row++) {
    unsigned actual_row = row + row_index;

    spanning_rows_height.row_height[row] = row_pos_[actual_row + 1] -
                                           row_pos_[actual_row] -
                                           BorderSpacingForRow(actual_row);
    if (!spanning_rows_height.row_height[row])
      spanning_rows_height.is_any_row_with_only_spanning_cells |=
          RowHasOnlySpanningCells(actual_row);

    spanning_rows_height.total_rows_height +=
        spanning_rows_height.row_height[row];
    spanning_rows_height.spanning_cell_height_ignoring_border_spacing -=
        BorderSpacingForRow(actual_row);
  }
  // We don't span the following row so its border-spacing (if any) should be
  // included.
  spanning_rows_height.spanning_cell_height_ignoring_border_spacing +=
      BorderSpacingForRow(row_index + row_span - 1);
}

void LayoutTableSection::DistributeExtraRowSpanHeightToPercentRows(
    LayoutTableCell* cell,
    float total_percent,
    int& extra_row_spanning_height,
    Vector<int>& rows_height) {
  if (!extra_row_spanning_height || !total_percent)
    return;

  const unsigned row_span = cell->RowSpan();
  const unsigned row_index = cell->RowIndex();
  float percent = std::min(total_percent, 100.0f);
  const int table_height = row_pos_[grid_.size()] + extra_row_spanning_height;

  // Our algorithm matches Firefox. Extra spanning height would be distributed
  // Only in first percent height rows those total percent is 100. Other percent
  // rows would be uneffected even extra spanning height is remain.
  int accumulated_position_increase = 0;
  for (unsigned row = row_index; row < (row_index + row_span); row++) {
    if (percent > 0 && extra_row_spanning_height > 0) {
      // TODO(alancutter): Make this work correctly for calc lengths.
      if (grid_[row].logical_height.IsPercent()) {
        int to_add =
            (table_height *
             std::min(grid_[row].logical_height.Percent(), percent) / 100) -
            rows_height[row - row_index];

        to_add = std::max(std::min(to_add, extra_row_spanning_height), 0);
        accumulated_position_increase += to_add;
        extra_row_spanning_height -= to_add;
        percent -= grid_[row].logical_height.Percent();
      }
    }
    row_pos_[row + 1] += accumulated_position_increase;
  }
}

static void UpdatePositionIncreasedWithRowHeight(
    int extra_height,
    float row_height,
    float total_height,
    int& accumulated_position_increase,
    double& remainder) {
  // Without the cast we lose enough precision to cause heights to miss pixels
  // (and trigger asserts) in some layout tests.
  double proportional_position_increase =
      remainder + (extra_height * double(row_height)) / total_height;
  // The epsilon is to push any values that are close to a whole number but
  // aren't due to floating point imprecision. The epsilons are not accumulated,
  // any that aren't necessary are lost in the cast to int.
  int position_increase_int = proportional_position_increase + 0.000001;
  accumulated_position_increase += position_increase_int;
  remainder = proportional_position_increase - position_increase_int;
}

// This is mainly used to distribute whole extra rowspanning height in percent
// rows when all spanning rows are percent rows.
// Distributing whole extra rowspanning height in percent rows based on the
// ratios of percent because this method works same as percent distribution when
// only percent rows are present and percent is 100. Also works perfectly fine
// when percent is not equal to 100.
void LayoutTableSection::DistributeWholeExtraRowSpanHeightToPercentRows(
    LayoutTableCell* cell,
    float total_percent,
    int& extra_row_spanning_height,
    Vector<int>& rows_height) {
  if (!extra_row_spanning_height || !total_percent)
    return;

  const unsigned row_span = cell->RowSpan();
  const unsigned row_index = cell->RowIndex();
  double remainder = 0;

  int accumulated_position_increase = 0;
  for (unsigned row = row_index; row < (row_index + row_span); row++) {
    // TODO(alancutter): Make this work correctly for calc lengths.
    if (grid_[row].logical_height.IsPercent()) {
      UpdatePositionIncreasedWithRowHeight(
          extra_row_spanning_height, grid_[row].logical_height.Percent(),
          total_percent, accumulated_position_increase, remainder);
    }
    row_pos_[row + 1] += accumulated_position_increase;
  }

  DCHECK(!round(remainder)) << "remainder was " << remainder;

  extra_row_spanning_height -= accumulated_position_increase;
}

void LayoutTableSection::DistributeExtraRowSpanHeightToAutoRows(
    LayoutTableCell* cell,
    int total_auto_rows_height,
    int& extra_row_spanning_height,
    Vector<int>& rows_height) {
  if (!extra_row_spanning_height || !total_auto_rows_height)
    return;

  const unsigned row_span = cell->RowSpan();
  const unsigned row_index = cell->RowIndex();
  int accumulated_position_increase = 0;
  double remainder = 0;

  // Aspect ratios of auto rows should not change otherwise table may look
  // different than user expected. So extra height distributed in auto spanning
  // rows based on their weight in spanning cell.
  for (unsigned row = row_index; row < (row_index + row_span); row++) {
    if (grid_[row].logical_height.IsAuto()) {
      UpdatePositionIncreasedWithRowHeight(
          extra_row_spanning_height, rows_height[row - row_index],
          total_auto_rows_height, accumulated_position_increase, remainder);
    }
    row_pos_[row + 1] += accumulated_position_increase;
  }

  DCHECK(!round(remainder)) << "remainder was " << remainder;

  extra_row_spanning_height -= accumulated_position_increase;
}

void LayoutTableSection::DistributeExtraRowSpanHeightToRemainingRows(
    LayoutTableCell* cell,
    int total_remaining_rows_height,
    int& extra_row_spanning_height,
    Vector<int>& rows_height) {
  if (!extra_row_spanning_height || !total_remaining_rows_height)
    return;

  const unsigned row_span = cell->RowSpan();
  const unsigned row_index = cell->RowIndex();
  int accumulated_position_increase = 0;
  double remainder = 0;

  // Aspect ratios of the rows should not change otherwise table may look
  // different than user expected. So extra height distribution in remaining
  // spanning rows based on their weight in spanning cell.
  for (unsigned row = row_index; row < (row_index + row_span); row++) {
    if (!grid_[row].logical_height.IsPercentOrCalc()) {
      UpdatePositionIncreasedWithRowHeight(
          extra_row_spanning_height, rows_height[row - row_index],
          total_remaining_rows_height, accumulated_position_increase,
          remainder);
    }
    row_pos_[row + 1] += accumulated_position_increase;
  }

  DCHECK(!round(remainder)) << "remainder was " << remainder;

  extra_row_spanning_height -= accumulated_position_increase;
}

static bool CellIsFullyIncludedInOtherCell(const LayoutTableCell* cell1,
                                           const LayoutTableCell* cell2) {
  return (cell1->RowIndex() >= cell2->RowIndex() &&
          (cell1->RowIndex() + cell1->RowSpan()) <=
              (cell2->RowIndex() + cell2->RowSpan()));
}

// To avoid unneeded extra height distributions, we apply the following sorting
// algorithm:
static bool CompareRowSpanCellsInHeightDistributionOrder(
    const LayoutTableCell* cell1,
    const LayoutTableCell* cell2) {
  // Sorting bigger height cell first if cells are at same index with same span
  // because we will skip smaller height cell to distribute it's extra height.
  if (cell1->RowIndex() == cell2->RowIndex() &&
      cell1->RowSpan() == cell2->RowSpan())
    return (cell1->LogicalHeightForRowSizing() >
            cell2->LogicalHeightForRowSizing());
  // Sorting inner most cell first because if inner spanning cell'e extra height
  // is distributed then outer spanning cell's extra height will adjust
  // accordingly. In reverse order, there is more chances that outer spanning
  // cell's height will exceed than defined by user.
  if (CellIsFullyIncludedInOtherCell(cell1, cell2))
    return true;
  // Sorting lower row index first because first we need to apply the extra
  // height of spanning cell which comes first in the table so lower rows's
  // position would increment in sequence.
  if (!CellIsFullyIncludedInOtherCell(cell2, cell1))
    return (cell1->RowIndex() < cell2->RowIndex());

  return false;
}

unsigned LayoutTableSection::CalcRowHeightHavingOnlySpanningCells(
    unsigned row,
    int& accumulated_cell_position_increase,
    unsigned row_to_apply_extra_height,
    unsigned& extra_table_height_to_propgate,
    Vector<int>& rows_count_with_only_spanning_cells) {
  DCHECK(RowHasOnlySpanningCells(row));

  unsigned total_cols = grid_[row].row.size();

  if (!total_cols)
    return 0;

  unsigned row_height = 0;

  for (unsigned col = 0; col < total_cols; col++) {
    const CellStruct& row_span_cell = CellAt(row, col);

    if (!row_span_cell.cells.size())
      continue;

    LayoutTableCell* cell = row_span_cell.cells[0];

    if (cell->RowSpan() < 2)
      continue;

    const unsigned cell_row_index = cell->RowIndex();
    const unsigned cell_row_span = cell->RowSpan();

    // As we are going from the top of the table to the bottom to calculate the
    // row heights for rows that only contain spanning cells and all previous
    // rows are processed we only need to find the number of rows with spanning
    // cells from the current cell to the end of the current cells spanning
    // height.
    unsigned start_row_for_spanning_cell_count = std::max(cell_row_index, row);
    unsigned end_row = cell_row_index + cell_row_span;
    unsigned spanning_cells_rows_count_having_zero_height =
        rows_count_with_only_spanning_cells[end_row - 1];

    if (start_row_for_spanning_cell_count)
      spanning_cells_rows_count_having_zero_height -=
          rows_count_with_only_spanning_cells
              [start_row_for_spanning_cell_count - 1];

    int total_rowspan_cell_height =
        (row_pos_[end_row] - row_pos_[cell_row_index]) -
        BorderSpacingForRow(end_row - 1);

    total_rowspan_cell_height += accumulated_cell_position_increase;
    if (row_to_apply_extra_height >= cell_row_index &&
        row_to_apply_extra_height < end_row)
      total_rowspan_cell_height += extra_table_height_to_propgate;

    if (total_rowspan_cell_height < cell->LogicalHeightForRowSizing()) {
      unsigned extra_height_required =
          cell->LogicalHeightForRowSizing() - total_rowspan_cell_height;

      row_height = std::max(
          row_height,
          extra_height_required / spanning_cells_rows_count_having_zero_height);
    }
  }

  return row_height;
}

void LayoutTableSection::UpdateRowsHeightHavingOnlySpanningCells(
    LayoutTableCell* cell,
    struct SpanningRowsHeight& spanning_rows_height,
    unsigned& extra_height_to_propagate,
    Vector<int>& rows_count_with_only_spanning_cells) {
  DCHECK(spanning_rows_height.row_height.size());

  int accumulated_position_increase = 0;
  const unsigned row_span = cell->RowSpan();
  const unsigned row_index = cell->RowIndex();

  DCHECK_EQ(row_span, spanning_rows_height.row_height.size());

  for (unsigned row = 0; row < spanning_rows_height.row_height.size(); row++) {
    unsigned actual_row = row + row_index;
    if (!spanning_rows_height.row_height[row] &&
        RowHasOnlySpanningCells(actual_row)) {
      spanning_rows_height.row_height[row] =
          CalcRowHeightHavingOnlySpanningCells(
              actual_row, accumulated_position_increase, row_index + row_span,
              extra_height_to_propagate, rows_count_with_only_spanning_cells);
      accumulated_position_increase += spanning_rows_height.row_height[row];
    }
    row_pos_[actual_row + 1] += accumulated_position_increase;
  }

  spanning_rows_height.total_rows_height += accumulated_position_increase;
}

// Distribute rowSpan cell height in rows those comes in rowSpan cell based on
// the ratio of row's height if 1 RowSpan cell height is greater than the total
// height of rows in rowSpan cell.
void LayoutTableSection::DistributeRowSpanHeightToRows(
    SpanningLayoutTableCells& row_span_cells) {
  DCHECK(row_span_cells.size());

  // 'rowSpanCells' list is already sorted based on the cells rowIndex in
  // ascending order
  // Arrange row spanning cell in the order in which we need to process first.
  std::sort(row_span_cells.begin(), row_span_cells.end(),
            CompareRowSpanCellsInHeightDistributionOrder);

  unsigned extra_height_to_propagate = 0;
  unsigned last_row_index = 0;
  unsigned last_row_span = 0;

  Vector<int> rows_count_with_only_spanning_cells;

  // At this stage, Height of the rows are zero for the one containing only
  // spanning cells.
  int count = 0;
  for (unsigned row = 0; row < grid_.size(); row++) {
    if (RowHasOnlySpanningCells(row))
      count++;
    rows_count_with_only_spanning_cells.push_back(count);
  }

  for (unsigned i = 0; i < row_span_cells.size(); i++) {
    LayoutTableCell* cell = row_span_cells[i];

    unsigned row_index = cell->RowIndex();

    unsigned row_span = cell->RowSpan();

    unsigned spanning_cell_end_index = row_index + row_span;
    unsigned last_spanning_cell_end_index = last_row_index + last_row_span;

    // Only the highest spanning cell will distribute its extra height in a row
    // if more than one spanning cell is present at the same level.
    if (row_index == last_row_index && row_span == last_row_span)
      continue;

    int original_before_position = row_pos_[spanning_cell_end_index];

    // When 2 spanning cells are ending at same row index then while extra
    // height distribution of first spanning cell updates position of the last
    // row so getting the original position of the last row in second spanning
    // cell need to reduce the height changed by first spanning cell.
    if (spanning_cell_end_index == last_spanning_cell_end_index)
      original_before_position -= extra_height_to_propagate;

    if (extra_height_to_propagate) {
      for (unsigned row = last_spanning_cell_end_index + 1;
           row <= spanning_cell_end_index; row++)
        row_pos_[row] += extra_height_to_propagate;
    }

    last_row_index = row_index;
    last_row_span = row_span;

    struct SpanningRowsHeight spanning_rows_height;

    PopulateSpanningRowsHeightFromCell(cell, spanning_rows_height);

    // Here we are handling only row(s) who have only rowspanning cells and do
    // not have any empty cell.
    if (spanning_rows_height.is_any_row_with_only_spanning_cells)
      UpdateRowsHeightHavingOnlySpanningCells(
          cell, spanning_rows_height, extra_height_to_propagate,
          rows_count_with_only_spanning_cells);

    // This code handle row(s) that have rowspanning cell(s) and at least one
    // empty cell. Such rows are not handled below and end up having a height of
    // 0. That would mean content overlapping if one of their cells has any
    // content. To avoid the problem, we add all the remaining spanning cells'
    // height to the last spanned row. This means that we could grow a row past
    // its 'height' or break percentage spreading however this is better than
    // overlapping content.
    // FIXME: Is there a better algorithm?
    if (!spanning_rows_height.total_rows_height) {
      if (spanning_rows_height.spanning_cell_height_ignoring_border_spacing)
        row_pos_[spanning_cell_end_index] +=
            spanning_rows_height.spanning_cell_height_ignoring_border_spacing +
            BorderSpacingForRow(spanning_cell_end_index - 1);

      extra_height_to_propagate =
          row_pos_[spanning_cell_end_index] - original_before_position;
      continue;
    }

    if (spanning_rows_height.spanning_cell_height_ignoring_border_spacing <=
        spanning_rows_height.total_rows_height) {
      extra_height_to_propagate =
          row_pos_[row_index + row_span] - original_before_position;
      continue;
    }

    // Below we are handling only row(s) who have at least one visible cell
    // without rowspan value.
    float total_percent = 0;
    int total_auto_rows_height = 0;
    int total_remaining_rows_height = spanning_rows_height.total_rows_height;

    // FIXME: Inner spanning cell height should not change if it have fixed
    // height when it's parent spanning cell is distributing it's extra height
    // in rows.

    // Calculate total percentage, total auto rows height and total rows height
    // except percent rows.
    for (unsigned row = row_index; row < spanning_cell_end_index; row++) {
      // TODO(alancutter): Make this work correctly for calc lengths.
      if (grid_[row].logical_height.IsPercent()) {
        total_percent += grid_[row].logical_height.Percent();
        total_remaining_rows_height -=
            spanning_rows_height.row_height[row - row_index];
      } else if (grid_[row].logical_height.IsAuto()) {
        total_auto_rows_height +=
            spanning_rows_height.row_height[row - row_index];
      }
    }

    int extra_row_spanning_height =
        spanning_rows_height.spanning_cell_height_ignoring_border_spacing -
        spanning_rows_height.total_rows_height;

    if (total_percent < 100 && !total_auto_rows_height &&
        !total_remaining_rows_height) {
      // Distributing whole extra rowspanning height in percent row when only
      // non-percent rows height is 0.
      DistributeWholeExtraRowSpanHeightToPercentRows(
          cell, total_percent, extra_row_spanning_height,
          spanning_rows_height.row_height);
    } else {
      DistributeExtraRowSpanHeightToPercentRows(
          cell, total_percent, extra_row_spanning_height,
          spanning_rows_height.row_height);
      DistributeExtraRowSpanHeightToAutoRows(cell, total_auto_rows_height,
                                             extra_row_spanning_height,
                                             spanning_rows_height.row_height);
      DistributeExtraRowSpanHeightToRemainingRows(
          cell, total_remaining_rows_height, extra_row_spanning_height,
          spanning_rows_height.row_height);
    }

    DCHECK(!extra_row_spanning_height);

    // Getting total changed height in the table
    extra_height_to_propagate =
        row_pos_[spanning_cell_end_index] - original_before_position;
  }

  if (extra_height_to_propagate) {
    // Apply changed height by rowSpan cells to rows present at the end of the
    // table
    for (unsigned row = last_row_index + last_row_span + 1; row <= grid_.size();
         row++)
      row_pos_[row] += extra_height_to_propagate;
  }
}

// Find out the baseline of the cell
// If the cell's baseline is more than the row's baseline then the cell's
// baseline become the row's baseline and if the row's baseline goes out of the
// row's boundaries then adjust row height accordingly.
void LayoutTableSection::UpdateBaselineForCell(LayoutTableCell* cell,
                                               unsigned row,
                                               int& baseline_descent) {
  if (!cell->IsBaselineAligned())
    return;

  // Ignoring the intrinsic padding as it depends on knowing the row's baseline,
  // which won't be accurate until the end of this function.
  int baseline_position =
      cell->CellBaselinePosition() - cell->IntrinsicPaddingBefore();
  if (baseline_position >
      cell->BorderBefore() +
          (cell->PaddingBefore() - cell->IntrinsicPaddingBefore())) {
    grid_[row].baseline = std::max(grid_[row].baseline, baseline_position);

    int cell_start_row_baseline_descent = 0;
    if (cell->RowSpan() == 1) {
      baseline_descent =
          std::max(baseline_descent,
                   cell->LogicalHeightForRowSizing() - baseline_position);
      cell_start_row_baseline_descent = baseline_descent;
    }
    row_pos_[row + 1] =
        std::max<int>(row_pos_[row + 1], row_pos_[row] + grid_[row].baseline +
                                             cell_start_row_baseline_descent);
  }
}

int LayoutTableSection::CalcRowLogicalHeight() {
#if DCHECK_IS_ON()
  SetLayoutNeededForbiddenScope layout_forbidden_scope(*this);
#endif

  DCHECK(!NeedsLayout());

  LayoutTableCell* cell;

  // We may have to forcefully lay out cells here, in which case we need a
  // layout state.
  LayoutState state(*this);

  row_pos_.Resize(grid_.size() + 1);

  // We ignore the border-spacing on any non-top section as it is already
  // included in the previous section's last row position.
  if (this == Table()->TopSection())
    row_pos_[0] = Table()->VBorderSpacing();
  else
    row_pos_[0] = 0;

  SpanningLayoutTableCells row_span_cells;

  // At fragmentainer breaks we need to prevent rowspanned cells (and whatever
  // else) from distributing their extra height requirements over the rows that
  // it spans. Otherwise we'd need to refragment afterwards.
  unsigned index_of_first_stretchable_row = 0;

  for (unsigned r = 0; r < grid_.size(); r++) {
    grid_[r].baseline = -1;
    int baseline_descent = 0;

    if (state.IsPaginated() && grid_[r].row_layout_object)
      row_pos_[r] += grid_[r].row_layout_object->PaginationStrut().Ceil();

    if (grid_[r].logical_height.IsSpecified()) {
      // Our base size is the biggest logical height from our cells' styles
      // (excluding row spanning cells).
      row_pos_[r + 1] =
          std::max(row_pos_[r] + MinimumValueForLength(grid_[r].logical_height,
                                                       LayoutUnit())
                                     .Round(),
                   0);
    } else {
      // Non-specified lengths are ignored because the row already accounts for
      // the cells intrinsic logical height.
      row_pos_[r + 1] = std::max(row_pos_[r], 0);
    }

    Row& row = grid_[r].row;
    unsigned total_cols = row.size();

    for (unsigned c = 0; c < total_cols; c++) {
      CellStruct& current = CellAt(r, c);
      if (current.in_col_span)
        continue;
      for (unsigned i = 0; i < current.cells.size(); i++) {
        cell = current.cells[i];

        // For row spanning cells, we only handle them for the first row they
        // span. This ensures we take their baseline into account.
        if (cell->RowIndex() != r)
          continue;

        if (r < index_of_first_stretchable_row ||
            (state.IsPaginated() &&
             CrossesPageBoundary(
                 LayoutUnit(row_pos_[r]),
                 LayoutUnit(cell->LogicalHeightForRowSizing())))) {
          // Entering or extending a range of unstretchable rows. We enter this
          // mode when a cell in a row crosses a fragmentainer boundary, and
          // we'll stay in this mode until we get to a row where we're past all
          // rowspanned cells that we encountered while in this mode.
          DCHECK(state.IsPaginated());
          unsigned row_index_below_cell = r + cell->RowSpan();
          index_of_first_stretchable_row =
              std::max(index_of_first_stretchable_row, row_index_below_cell);
        } else if (cell->RowSpan() > 1) {
          DCHECK(!row_span_cells.Contains(cell));
          row_span_cells.push_back(cell);
        }

        if (cell->HasOverrideLogicalContentHeight()) {
          cell->ClearIntrinsicPadding();
          cell->ClearOverrideSize();
          cell->ForceChildLayout();
        }

        if (cell->RowSpan() == 1)
          row_pos_[r + 1] = std::max(
              row_pos_[r + 1], row_pos_[r] + cell->LogicalHeightForRowSizing());

        // Find out the baseline. The baseline is set on the first row in a
        // rowSpan.
        UpdateBaselineForCell(cell, r, baseline_descent);
      }
    }

    if (r < index_of_first_stretchable_row && grid_[r].row_layout_object) {
      // We're not allowed to resize this row. Just scratch what we've
      // calculated so far, and use the height that we got during initial
      // layout instead.
      row_pos_[r + 1] =
          row_pos_[r] + grid_[r].row_layout_object->LogicalHeight().ToInt();
    }

    // Add the border-spacing to our final position.
    row_pos_[r + 1] += BorderSpacingForRow(r);
    row_pos_[r + 1] = std::max(row_pos_[r + 1], row_pos_[r]);
  }

  if (!row_span_cells.IsEmpty())
    DistributeRowSpanHeightToRows(row_span_cells);

  DCHECK(!NeedsLayout());

  return row_pos_[grid_.size()];
}

void LayoutTableSection::UpdateLayout() {
  DCHECK(NeedsLayout());
  LayoutAnalyzer::Scope analyzer(*this);
  CHECK(!NeedsCellRecalc());
  DCHECK(!Table()->NeedsSectionRecalc());

  // addChild may over-grow m_grid but we don't want to throw away the memory
  // too early as addChild can be called in a loop (e.g during parsing). Doing
  // it now ensures we have a stable-enough structure.
  grid_.ShrinkToFit();

  LayoutState state(*this);

  const Vector<int>& column_pos = Table()->EffectiveColumnPositions();
  LayoutUnit row_logical_top;

  SubtreeLayoutScope layouter(*this);
  for (unsigned r = 0; r < grid_.size(); ++r) {
    Row& row = grid_[r].row;
    unsigned cols = row.size();
    // First, propagate our table layout's information to the cells. This will
    // mark the row as needing layout if there was a column logical width
    // change.
    for (unsigned start_column = 0; start_column < cols; ++start_column) {
      CellStruct& current = row[start_column];
      LayoutTableCell* cell = current.PrimaryCell();
      if (!cell || current.in_col_span)
        continue;

      unsigned end_col = start_column;
      unsigned cspan = cell->ColSpan();
      while (cspan && end_col < cols) {
        DCHECK_LT(end_col, Table()->EffectiveColumns().size());
        cspan -= Table()->EffectiveColumns()[end_col].span;
        end_col++;
      }
      int table_layout_logical_width = column_pos[end_col] -
                                       column_pos[start_column] -
                                       Table()->HBorderSpacing();
      cell->SetCellLogicalWidth(table_layout_logical_width, layouter);
    }

    if (LayoutTableRow* row_layout_object = grid_[r].row_layout_object) {
      if (state.IsPaginated())
        row_layout_object->SetLogicalTop(row_logical_top);
      if (!row_layout_object->NeedsLayout())
        MarkChildForPaginationRelayoutIfNeeded(*row_layout_object, layouter);
      row_layout_object->LayoutIfNeeded();
      if (state.IsPaginated()) {
        AdjustRowForPagination(*row_layout_object, layouter);
        UpdateFragmentationInfoForChild(*row_layout_object);
        row_logical_top = row_layout_object->LogicalBottom();
        row_logical_top += LayoutUnit(Table()->VBorderSpacing());
      }
    }
  }

  ClearNeedsLayout();
}

void LayoutTableSection::DistributeExtraLogicalHeightToPercentRows(
    int& extra_logical_height,
    int total_percent) {
  if (!total_percent)
    return;

  unsigned total_rows = grid_.size();
  int total_height = row_pos_[total_rows] + extra_logical_height;
  int total_logical_height_added = 0;
  total_percent = std::min(total_percent, 100);
  int row_height = row_pos_[1] - row_pos_[0];
  for (unsigned r = 0; r < total_rows; ++r) {
    // TODO(alancutter): Make this work correctly for calc lengths.
    if (total_percent > 0 && grid_[r].logical_height.IsPercent()) {
      int to_add = std::min<int>(
          extra_logical_height,
          (total_height * grid_[r].logical_height.Percent() / 100) -
              row_height);
      // If toAdd is negative, then we don't want to shrink the row (this bug
      // affected Outlook Web Access).
      to_add = std::max(0, to_add);
      total_logical_height_added += to_add;
      extra_logical_height -= to_add;
      total_percent -= grid_[r].logical_height.Percent();
    }
    DCHECK_GE(total_rows, 1u);
    if (r < total_rows - 1)
      row_height = row_pos_[r + 2] - row_pos_[r + 1];
    row_pos_[r + 1] += total_logical_height_added;
  }
}

void LayoutTableSection::DistributeExtraLogicalHeightToAutoRows(
    int& extra_logical_height,
    unsigned auto_rows_count) {
  if (!auto_rows_count)
    return;

  int total_logical_height_added = 0;
  for (unsigned r = 0; r < grid_.size(); ++r) {
    if (auto_rows_count > 0 && grid_[r].logical_height.IsAuto()) {
      // Recomputing |extraLogicalHeightForRow| guarantees that we properly
      // ditribute round |extraLogicalHeight|.
      int extra_logical_height_for_row = extra_logical_height / auto_rows_count;
      total_logical_height_added += extra_logical_height_for_row;
      extra_logical_height -= extra_logical_height_for_row;
      --auto_rows_count;
    }
    row_pos_[r + 1] += total_logical_height_added;
  }
}

void LayoutTableSection::DistributeRemainingExtraLogicalHeight(
    int& extra_logical_height) {
  unsigned total_rows = grid_.size();

  if (extra_logical_height <= 0 || !row_pos_[total_rows])
    return;

  // FIXME: m_rowPos[totalRows] - m_rowPos[0] is the total rows' size.
  int total_row_size = row_pos_[total_rows];
  int total_logical_height_added = 0;
  int previous_row_position = row_pos_[0];
  for (unsigned r = 0; r < total_rows; r++) {
    // weight with the original height
    total_logical_height_added += extra_logical_height *
                                  (row_pos_[r + 1] - previous_row_position) /
                                  total_row_size;
    previous_row_position = row_pos_[r + 1];
    row_pos_[r + 1] += total_logical_height_added;
  }

  extra_logical_height -= total_logical_height_added;
}

int LayoutTableSection::DistributeExtraLogicalHeightToRows(
    int extra_logical_height) {
  if (!extra_logical_height)
    return extra_logical_height;

  unsigned total_rows = grid_.size();
  if (!total_rows)
    return extra_logical_height;

  if (!row_pos_[total_rows] && NextSibling())
    return extra_logical_height;

  unsigned auto_rows_count = 0;
  int total_percent = 0;
  for (unsigned r = 0; r < total_rows; r++) {
    if (grid_[r].logical_height.IsAuto())
      ++auto_rows_count;
    else if (grid_[r].logical_height.IsPercent())
      total_percent += grid_[r].logical_height.Percent();
  }

  int remaining_extra_logical_height = extra_logical_height;
  DistributeExtraLogicalHeightToPercentRows(remaining_extra_logical_height,
                                            total_percent);
  DistributeExtraLogicalHeightToAutoRows(remaining_extra_logical_height,
                                         auto_rows_count);
  DistributeRemainingExtraLogicalHeight(remaining_extra_logical_height);
  return extra_logical_height - remaining_extra_logical_height;
}

static bool ShouldFlexCellChild(const LayoutTableCell& cell,
                                LayoutObject* cell_descendant) {
  if (!cell.Style()->LogicalHeight().IsSpecified())
    return false;
  if (cell_descendant->Style()->OverflowY() == EOverflow::kVisible ||
      cell_descendant->Style()->OverflowY() == EOverflow::kHidden)
    return true;
  return cell_descendant->IsBox() &&
         ToLayoutBox(cell_descendant)->ShouldBeConsideredAsReplaced();
}

void LayoutTableSection::LayoutRows() {
#if DCHECK_IS_ON()
  SetLayoutNeededForbiddenScope layout_forbidden_scope(*this);
#endif

  DCHECK(!NeedsLayout());

  LayoutAnalyzer::Scope analyzer(*this);

  // FIXME: Changing the height without a layout can change the overflow so it
  // seems wrong.

  unsigned total_rows = grid_.size();

  // Set the width of our section now.  The rows will also be this width.
  SetLogicalWidth(Table()->ContentLogicalWidth());

  int vspacing = Table()->VBorderSpacing();
  LayoutState state(*this);

  // Set the rows' location and size.
  for (unsigned r = 0; r < total_rows; r++) {
    LayoutTableRow* row_layout_object = grid_[r].row_layout_object;
    if (row_layout_object) {
      row_layout_object->SetLogicalLocation(LayoutPoint(0, row_pos_[r]));
      row_layout_object->SetLogicalWidth(LogicalWidth());
      LayoutUnit row_logical_height(row_pos_[r + 1] - row_pos_[r] - vspacing);
      if (state.IsPaginated() && r + 1 < total_rows) {
        // If the next row has a pagination strut, we need to subtract it. It
        // should not be included in this row's height.
        if (LayoutTableRow* next_row_object = grid_[r + 1].row_layout_object)
          row_logical_height -= next_row_object->PaginationStrut();
      }
      row_layout_object->SetLogicalHeight(row_logical_height);
      row_layout_object->UpdateLayerTransformAfterLayout();
    }
  }

  // Vertically align and flex the cells in each row.
  for (unsigned r = 0; r < total_rows; r++) {
    LayoutTableRow* row_layout_object = grid_[r].row_layout_object;

    unsigned n_cols = NumCols(r);
    for (unsigned c = 0; c < n_cols; c++) {
      LayoutTableCell* cell = OriginatingCellAt(r, c);
      if (!cell)
        continue;

      int r_height;
      int row_logical_top;
      unsigned row_span = std::max(1U, cell->RowSpan());
      unsigned end_row_index = std::min(r + row_span, total_rows) - 1;
      LayoutTableRow* last_row_object = grid_[end_row_index].row_layout_object;
      if (last_row_object && row_layout_object) {
        row_logical_top = row_layout_object->LogicalTop().ToInt();
        r_height = last_row_object->LogicalBottom().ToInt() - row_logical_top;
      } else {
        r_height = row_pos_[end_row_index + 1] - row_pos_[r] - vspacing;
        row_logical_top = row_pos_[r];
      }

      RelayoutCellIfFlexed(*cell, r, r_height);

      SubtreeLayoutScope layouter(*cell);
      EVerticalAlign cell_vertical_align;
      // If the cell crosses a fragmentainer boundary, just align it at the
      // top. That's how it was laid out initially, before we knew the final
      // row height, and re-aligning it now could result in the cell being
      // fragmented differently, which could change its height and thus violate
      // the requested alignment. Give up instead of risking circular
      // dependencies and unstable layout.
      if (state.IsPaginated() &&
          CrossesPageBoundary(LayoutUnit(row_logical_top),
                              LayoutUnit(r_height)))
        cell_vertical_align = EVerticalAlign::kTop;
      else
        cell_vertical_align = cell->Style()->VerticalAlign();
      cell->ComputeIntrinsicPadding(r_height, cell_vertical_align, layouter);

      LayoutRect old_cell_rect = cell->FrameRect();

      SetLogicalPositionForCell(cell, c);

      cell->LayoutIfNeeded();

      LayoutSize child_offset(cell->Location() - old_cell_rect.Location());
      if (child_offset.Width() || child_offset.Height()) {
        // If the child moved, we have to issue paint invalidations to it as
        // well as any floating/positioned descendants. An exception is if we
        // need a layout. In this case, we know we're going to issue paint
        // invalidations ourselves (and the child) anyway.
        if (!Table()->SelfNeedsLayout())
          cell->SetMayNeedPaintInvalidation();
      }
    }
    if (row_layout_object)
      row_layout_object->ComputeOverflow();
  }

  DCHECK(!NeedsLayout());

  SetLogicalHeight(LayoutUnit(row_pos_[total_rows]));

  ComputeOverflowFromCells(total_rows, Table()->NumEffectiveColumns());
}

int LayoutTableSection::PaginationStrutForRow(LayoutTableRow* row,
                                              LayoutUnit logical_offset) const {
  DCHECK(row);
  if (row->GetPaginationBreakability() == kAllowAnyBreaks)
    return 0;
  LayoutUnit page_logical_height = PageLogicalHeightForOffset(logical_offset);
  if (!page_logical_height)
    return 0;
  // If the row is too tall for the page don't insert a strut.
  LayoutUnit row_logical_height = row->LogicalHeight();
  if (row_logical_height > page_logical_height)
    return 0;

  LayoutUnit remaining_logical_height = PageRemainingLogicalHeightForOffset(
      logical_offset, LayoutBlock::kAssociateWithLatterPage);
  if (remaining_logical_height >= row_logical_height)
    return 0;  // It fits fine where it is. No need to break.
  LayoutUnit pagination_strut = CalculatePaginationStrutToFitContent(
      logical_offset, remaining_logical_height, row_logical_height);
  if (pagination_strut == remaining_logical_height &&
      remaining_logical_height == page_logical_height) {
    // Don't break if we were at the top of a page, and we failed to fit the
    // content completely. No point in leaving a page completely blank.
    return 0;
  }
  // Table layout parts only work on integers, so we have to round. Round up, to
  // make sure that no fraction ever gets left behind in the previous
  // fragmentainer.
  return pagination_strut.Ceil();
}

void LayoutTableSection::ComputeOverflowFromCells() {
  unsigned total_rows = grid_.size();
  unsigned n_eff_cols = Table()->NumEffectiveColumns();
  ComputeOverflowFromCells(total_rows, n_eff_cols);
}

void LayoutTableSection::ComputeOverflowFromCells(unsigned total_rows,
                                                  unsigned n_eff_cols) {
  unsigned total_cells_count = n_eff_cols * total_rows;
  unsigned max_allowed_overflowing_cells_count =
      total_cells_count <
              g_min_table_size_to_use_fast_paint_path_with_overflowing_cell
          ? 0
          : kGMaxAllowedOverflowingCellRatioForFastPaintPath *
                total_cells_count;

  overflow_.reset();
  overflowing_cells_.Clear();
  force_slow_paint_path_with_overflowing_cell_ = false;
#if DCHECK_IS_ON()
  bool has_overflowing_cell = false;
#endif
  // Now that our height has been determined, add in overflow from cells.
  for (unsigned r = 0; r < total_rows; r++) {
    unsigned n_cols = NumCols(r);
    for (unsigned c = 0; c < n_cols; c++) {
      const auto* cell = OriginatingCellAt(r, c);
      if (!cell)
        continue;
      AddOverflowFromChild(*cell);
#if DCHECK_IS_ON()
      has_overflowing_cell |= cell->HasVisualOverflow();
#endif
      if (cell->HasVisualOverflow() &&
          !force_slow_paint_path_with_overflowing_cell_) {
        overflowing_cells_.insert(cell);
        if (overflowing_cells_.size() > max_allowed_overflowing_cells_count) {
          // We need to set m_forcesSlowPaintPath only if there is a least one
          // overflowing cells as the hit testing code rely on this information.
          force_slow_paint_path_with_overflowing_cell_ = true;
          // The slow path does not make any use of the overflowing cells info,
          // don't hold on to the memory.
          overflowing_cells_.Clear();
        }
      }
    }
  }

#if DCHECK_IS_ON()
  DCHECK_EQ(has_overflowing_cell, this->HasOverflowingCell());
#endif
}

bool LayoutTableSection::RecalcChildOverflowAfterStyleChange() {
  DCHECK(ChildNeedsOverflowRecalcAfterStyleChange());
  ClearChildNeedsOverflowRecalcAfterStyleChange();
  unsigned total_rows = grid_.size();
  bool children_overflow_changed = false;
  for (unsigned r = 0; r < total_rows; r++) {
    LayoutTableRow* row_layouter = RowLayoutObjectAt(r);
    if (!row_layouter ||
        !row_layouter->ChildNeedsOverflowRecalcAfterStyleChange())
      continue;
    row_layouter->ClearChildNeedsOverflowRecalcAfterStyleChange();
    bool row_children_overflow_changed = false;
    unsigned n_cols = NumCols(r);
    for (unsigned c = 0; c < n_cols; c++) {
      auto* cell = OriginatingCellAt(r, c);
      if (!cell || !cell->NeedsOverflowRecalcAfterStyleChange())
        continue;
      row_children_overflow_changed |= cell->RecalcOverflowAfterStyleChange();
    }
    if (row_children_overflow_changed)
      row_layouter->ComputeOverflow();
    children_overflow_changed |= row_children_overflow_changed;
  }
  // TODO(crbug.com/604136): Add visual overflow from rows too.
  if (children_overflow_changed)
    ComputeOverflowFromCells(total_rows, Table()->NumEffectiveColumns());
  return children_overflow_changed;
}

void LayoutTableSection::MarkAllCellsWidthsDirtyAndOrNeedsLayout(
    LayoutTable::WhatToMarkAllCells what_to_mark) {
  for (LayoutTableRow* row = FirstRow(); row; row = row->NextRow()) {
    for (LayoutTableCell* cell = row->FirstCell(); cell;
         cell = cell->NextCell()) {
      cell->SetPreferredLogicalWidthsDirty();
      if (what_to_mark == LayoutTable::kMarkDirtyAndNeedsLayout)
        cell->SetChildNeedsLayout();
    }
  }
}

int LayoutTableSection::CalcBlockDirectionOuterBorder(
    BlockBorderSide side) const {
  if (!grid_.size() || !Table()->NumEffectiveColumns())
    return 0;

  int border_width = 0;

  const BorderValue& sb =
      side == kBorderBefore ? Style()->BorderBefore() : Style()->BorderAfter();
  if (sb.Style() == kBorderStyleHidden)
    return -1;
  if (sb.Style() > kBorderStyleHidden)
    border_width = sb.Width();

  const BorderValue& rb = side == kBorderBefore
                              ? FirstRow()->Style()->BorderBefore()
                              : LastRow()->Style()->BorderAfter();
  if (rb.Style() == kBorderStyleHidden)
    return -1;
  if (rb.Style() > kBorderStyleHidden && rb.Width() > border_width)
    border_width = rb.Width();

  bool all_hidden = true;
  unsigned r = side == kBorderBefore ? 0 : grid_.size() - 1;
  unsigned n_cols = NumCols(r);
  for (unsigned c = 0; c < n_cols; c++) {
    const CellStruct& current = CellAt(r, c);
    if (current.in_col_span || !current.HasCells())
      continue;
    const ComputedStyle& primary_cell_style = current.PrimaryCell()->StyleRef();
    // FIXME: Make this work with perpendicular and flipped cells.
    const BorderValue& cb = side == kBorderBefore
                                ? primary_cell_style.BorderBefore()
                                : primary_cell_style.BorderAfter();
    // FIXME: Don't repeat for the same col group
    LayoutTableCol* col =
        Table()->ColElementAtAbsoluteColumn(c).InnermostColOrColGroup();
    if (col) {
      const BorderValue& gb = side == kBorderBefore
                                  ? col->Style()->BorderBefore()
                                  : col->Style()->BorderAfter();
      if (gb.Style() == kBorderStyleHidden || cb.Style() == kBorderStyleHidden)
        continue;
      all_hidden = false;
      if (gb.Style() > kBorderStyleHidden && gb.Width() > border_width)
        border_width = gb.Width();
      if (cb.Style() > kBorderStyleHidden && cb.Width() > border_width)
        border_width = cb.Width();
    } else {
      if (cb.Style() == kBorderStyleHidden)
        continue;
      all_hidden = false;
      if (cb.Style() > kBorderStyleHidden && cb.Width() > border_width)
        border_width = cb.Width();
    }
  }
  if (all_hidden)
    return -1;

  if (side == kBorderAfter)
    border_width++;  // Distribute rounding error
  return border_width / 2;
}

int LayoutTableSection::CalcInlineDirectionOuterBorder(
    InlineBorderSide side) const {
  unsigned total_cols = Table()->NumEffectiveColumns();
  if (!grid_.size() || !total_cols)
    return 0;
  unsigned col_index = side == kBorderStart ? 0 : total_cols - 1;

  int border_width = 0;

  const BorderValue& sb =
      side == kBorderStart ? Style()->BorderStart() : Style()->BorderEnd();
  if (sb.Style() == kBorderStyleHidden)
    return -1;
  if (sb.Style() > kBorderStyleHidden)
    border_width = sb.Width();

  if (LayoutTableCol* col = Table()
                                ->ColElementAtAbsoluteColumn(col_index)
                                .InnermostColOrColGroup()) {
    const BorderValue& gb = side == kBorderStart ? col->Style()->BorderStart()
                                                 : col->Style()->BorderEnd();
    if (gb.Style() == kBorderStyleHidden)
      return -1;
    if (gb.Style() > kBorderStyleHidden && gb.Width() > border_width)
      border_width = gb.Width();
  }

  bool all_hidden = true;
  for (unsigned r = 0; r < grid_.size(); r++) {
    if (col_index >= NumCols(r))
      continue;
    const CellStruct& current = CellAt(r, col_index);
    if (!current.HasCells())
      continue;
    // FIXME: Don't repeat for the same cell
    const ComputedStyle& primary_cell_style = current.PrimaryCell()->StyleRef();
    const ComputedStyle& primary_cell_parent_style =
        current.PrimaryCell()->Parent()->StyleRef();
    // FIXME: Make this work with perpendicular and flipped cells.
    const BorderValue& cb = side == kBorderStart
                                ? primary_cell_style.BorderStart()
                                : primary_cell_style.BorderEnd();
    const BorderValue& rb = side == kBorderStart
                                ? primary_cell_parent_style.BorderStart()
                                : primary_cell_parent_style.BorderEnd();
    if (cb.Style() == kBorderStyleHidden || rb.Style() == kBorderStyleHidden)
      continue;
    all_hidden = false;
    if (cb.Style() > kBorderStyleHidden && cb.Width() > border_width)
      border_width = cb.Width();
    if (rb.Style() > kBorderStyleHidden && rb.Width() > border_width)
      border_width = rb.Width();
  }
  if (all_hidden)
    return -1;

  if ((side == kBorderStart) != Table()->Style()->IsLeftToRightDirection())
    border_width++;  // Distribute rounding error
  return border_width / 2;
}

void LayoutTableSection::RecalcOuterBorder() {
  outer_border_before_ = CalcBlockDirectionOuterBorder(kBorderBefore);
  outer_border_after_ = CalcBlockDirectionOuterBorder(kBorderAfter);
  outer_border_start_ = CalcInlineDirectionOuterBorder(kBorderStart);
  outer_border_end_ = CalcInlineDirectionOuterBorder(kBorderEnd);
}

int LayoutTableSection::FirstLineBoxBaseline() const {
  if (!grid_.size())
    return -1;

  int first_line_baseline = grid_[0].baseline;
  if (first_line_baseline >= 0)
    return first_line_baseline + row_pos_[0];

  const Row& first_row = grid_[0].row;
  for (size_t i = 0; i < first_row.size(); ++i) {
    const CellStruct& cs = first_row.at(i);
    const LayoutTableCell* cell = cs.PrimaryCell();
    if (cell)
      first_line_baseline =
          std::max<int>(first_line_baseline,
                        (cell->LogicalTop() + cell->BorderBefore() +
                         cell->PaddingBefore() + cell->ContentLogicalHeight())
                            .ToInt());
  }

  return first_line_baseline;
}

void LayoutTableSection::Paint(const PaintInfo& paint_info,
                               const LayoutPoint& paint_offset) const {
  TableSectionPainter(*this).Paint(paint_info, paint_offset);
}

LayoutRect LayoutTableSection::LogicalRectForWritingModeAndDirection(
    const LayoutRect& rect) const {
  LayoutRect table_aligned_rect(rect);

  FlipForWritingMode(table_aligned_rect);

  if (!Style()->IsHorizontalWritingMode())
    table_aligned_rect = table_aligned_rect.TransposedRect();

  const Vector<int>& column_pos = Table()->EffectiveColumnPositions();
  // FIXME: The table's direction should determine our row's direction, not the
  // section's (see bug 96691).
  if (!Style()->IsLeftToRightDirection())
    table_aligned_rect.SetX(column_pos[column_pos.size() - 1] -
                            table_aligned_rect.MaxX());

  return table_aligned_rect;
}

CellSpan LayoutTableSection::DirtiedRows(const LayoutRect& damage_rect) const {
  if (force_slow_paint_path_with_overflowing_cell_)
    return FullSectionRowSpan();

  if (!grid_.size())
    return CellSpan(0, 0);

  CellSpan covered_rows = SpannedRows(damage_rect);

  // To issue paint invalidations for the border we might need to paint
  // invalidate the first or last row even if they are not spanned themselves.
  CHECK_LT(covered_rows.Start(), row_pos_.size());
  if (covered_rows.Start() == row_pos_.size() - 1 &&
      row_pos_[row_pos_.size() - 1] + Table()->OuterBorderAfter() >=
          damage_rect.Y())
    covered_rows.DecreaseStart();

  if (!covered_rows.end() &&
      row_pos_[0] - Table()->OuterBorderBefore() <= damage_rect.MaxY())
    covered_rows.IncreaseEnd();

  covered_rows.EnsureConsistency(grid_.size());
  if (!has_spanning_cells_ || !covered_rows.Start() ||
      covered_rows.Start() >= grid_.size())
    return covered_rows;

  // If there are any cells spanning into the first row, expand coveredRows
  // to cover the primary cells.
  unsigned n_cols = NumCols(covered_rows.Start());
  unsigned smallest_row = covered_rows.Start();
  CellSpan covered_columns = SpannedEffectiveColumns(damage_rect);
  for (unsigned c = covered_columns.Start();
       c < std::min(covered_columns.end(), n_cols); ++c) {
    if (const auto* cell = PrimaryCellAt(covered_rows.Start(), c)) {
      smallest_row = std::min(smallest_row, cell->RowIndex());
      if (!smallest_row)
        break;
    }
  }
  return CellSpan(smallest_row, covered_rows.end());
}

CellSpan LayoutTableSection::DirtiedEffectiveColumns(
    const LayoutRect& damage_rect) const {
  if (force_slow_paint_path_with_overflowing_cell_)
    return FullTableEffectiveColumnSpan();

  CHECK(Table()->NumEffectiveColumns());
  CellSpan covered_columns = SpannedEffectiveColumns(damage_rect);

  const Vector<int>& column_pos = Table()->EffectiveColumnPositions();
  // To issue paint invalidations for the border we might need to paint
  // invalidate the first or last column even if they are not spanned
  // themselves.
  CHECK_LT(covered_columns.Start(), column_pos.size());
  if (covered_columns.Start() == column_pos.size() - 1 &&
      column_pos[column_pos.size() - 1] + Table()->OuterBorderEnd() >=
          damage_rect.X())
    covered_columns.DecreaseStart();

  if (!covered_columns.end() &&
      column_pos[0] - Table()->OuterBorderStart() <= damage_rect.MaxX())
    covered_columns.IncreaseEnd();

  covered_columns.EnsureConsistency(Table()->NumEffectiveColumns());
  if (!has_spanning_cells_ || !covered_columns.Start())
    return covered_columns;

  // If there are any cells spanning into the first column, expand
  // coveredRows to cover the primary cells.
  unsigned smallest_column = covered_columns.Start();
  CellSpan covered_rows = SpannedRows(damage_rect);
  for (unsigned r = covered_rows.Start(); r < covered_rows.end(); ++r) {
    const auto& row = grid_[r].row;
    if (covered_columns.Start() < row.size()) {
      unsigned c = covered_columns.Start();
      while (c && row[c].in_col_span)
        --c;
      smallest_column = std::min(c, smallest_column);
      if (!smallest_column)
        break;
    }
  }
  return CellSpan(smallest_column, covered_columns.end());
}

CellSpan LayoutTableSection::SpannedRows(const LayoutRect& flipped_rect) const {
  // Find the first row that starts after rect top.
  unsigned next_row =
      std::upper_bound(row_pos_.begin(), row_pos_.end(), flipped_rect.Y()) -
      row_pos_.begin();

  // After all rows.
  if (next_row == row_pos_.size())
    return CellSpan(row_pos_.size() - 1, row_pos_.size() - 1);

  unsigned start_row = next_row > 0 ? next_row - 1 : 0;

  // Find the first row that starts after rect bottom.
  unsigned end_row;
  if (row_pos_[next_row] >= flipped_rect.MaxY()) {
    end_row = next_row;
  } else {
    end_row = std::upper_bound(row_pos_.begin() + next_row, row_pos_.end(),
                               flipped_rect.MaxY()) -
              row_pos_.begin();
    if (end_row == row_pos_.size())
      end_row = row_pos_.size() - 1;
  }

  return CellSpan(start_row, end_row);
}

CellSpan LayoutTableSection::SpannedEffectiveColumns(
    const LayoutRect& flipped_rect) const {
  const Vector<int>& column_pos = Table()->EffectiveColumnPositions();

  // Find the first column that starts after rect left.
  // lower_bound doesn't handle the edge between two cells properly as it would
  // wrongly return the cell on the logical top/left.
  // upper_bound on the other hand properly returns the cell on the logical
  // bottom/right, which also matches the behavior of other browsers.
  unsigned next_column =
      std::upper_bound(column_pos.begin(), column_pos.end(), flipped_rect.X()) -
      column_pos.begin();

  if (next_column == column_pos.size())
    return CellSpan(column_pos.size() - 1,
                    column_pos.size() - 1);  // After all columns.

  unsigned start_column = next_column > 0 ? next_column - 1 : 0;

  // Find the first column that starts after rect right.
  unsigned end_column;
  if (column_pos[next_column] >= flipped_rect.MaxX()) {
    end_column = next_column;
  } else {
    end_column = std::upper_bound(column_pos.begin() + next_column,
                                  column_pos.end(), flipped_rect.MaxX()) -
                 column_pos.begin();
    if (end_column == column_pos.size())
      end_column = column_pos.size() - 1;
  }

  return CellSpan(start_column, end_column);
}

void LayoutTableSection::RecalcCells() {
  DCHECK(needs_cell_recalc_);
  // We reset the flag here to ensure that |addCell| works. This is safe to do
  // as fillRowsWithDefaultStartingAtPosition makes sure we match the table's
  // columns representation.
  needs_cell_recalc_ = false;

  c_col_ = 0;
  c_row_ = 0;
  grid_.Clear();

  for (LayoutTableRow* row = FirstRow(); row; row = row->NextRow()) {
    unsigned insertion_row = c_row_;
    ++c_row_;
    c_col_ = 0;
    EnsureRows(c_row_);

    grid_[insertion_row].row_layout_object = row;
    row->SetRowIndex(insertion_row);
    SetRowLogicalHeightToRowStyleLogicalHeight(grid_[insertion_row]);

    for (LayoutTableCell* cell = row->FirstCell(); cell;
         cell = cell->NextCell())
      AddCell(cell, row);
  }

  grid_.ShrinkToFit();
  SetNeedsLayoutAndFullPaintInvalidation(LayoutInvalidationReason::kUnknown);
}

// FIXME: This function could be made O(1) in certain cases (like for the
// non-most-constrainive cells' case).
void LayoutTableSection::RowLogicalHeightChanged(LayoutTableRow* row) {
  if (NeedsCellRecalc())
    return;

  unsigned row_index = row->RowIndex();
  SetRowLogicalHeightToRowStyleLogicalHeight(grid_[row_index]);

  for (LayoutTableCell* cell = grid_[row_index].row_layout_object->FirstCell();
       cell; cell = cell->NextCell())
    UpdateLogicalHeightForCell(grid_[row_index], cell);
}

void LayoutTableSection::SetNeedsCellRecalc() {
  needs_cell_recalc_ = true;
  if (LayoutTable* t = Table())
    t->SetNeedsSectionRecalc();
}

unsigned LayoutTableSection::NumEffectiveColumns() const {
  unsigned result = 0;

  for (unsigned r = 0; r < grid_.size(); ++r) {
    unsigned n_cols = NumCols(r);
    for (unsigned c = result; c < n_cols; ++c) {
      const CellStruct& cell = CellAt(r, c);
      if (cell.HasCells() || cell.in_col_span)
        result = c;
    }
  }

  return result + 1;
}

const BorderValue& LayoutTableSection::BorderAdjoiningStartCell(
    const LayoutTableCell* cell) const {
#if DCHECK_IS_ON()
  DCHECK(cell->IsFirstOrLastCellInRow());
#endif
  return HasSameDirectionAs(cell) ? Style()->BorderStart()
                                  : Style()->BorderEnd();
}

const BorderValue& LayoutTableSection::BorderAdjoiningEndCell(
    const LayoutTableCell* cell) const {
#if DCHECK_IS_ON()
  DCHECK(cell->IsFirstOrLastCellInRow());
#endif
  return HasSameDirectionAs(cell) ? Style()->BorderEnd()
                                  : Style()->BorderStart();
}

const LayoutTableCell* LayoutTableSection::FirstRowCellAdjoiningTableStart()
    const {
  unsigned adjoining_start_cell_column_index =
      HasSameDirectionAs(Table()) ? 0 : Table()->LastEffectiveColumnIndex();
  return PrimaryCellAt(0, adjoining_start_cell_column_index);
}

const LayoutTableCell* LayoutTableSection::FirstRowCellAdjoiningTableEnd()
    const {
  unsigned adjoining_end_cell_column_index =
      HasSameDirectionAs(Table()) ? Table()->LastEffectiveColumnIndex() : 0;
  return PrimaryCellAt(0, adjoining_end_cell_column_index);
}

LayoutTableCell* LayoutTableSection::OriginatingCellAt(
    unsigned row,
    unsigned effective_column) {
  auto& row_vector = grid_[row].row;
  if (effective_column >= row_vector.size())
    return nullptr;
  auto& cell_struct = row_vector[effective_column];
  if (cell_struct.in_col_span)
    return nullptr;
  if (auto* cell = cell_struct.PrimaryCell()) {
    if (cell->RowIndex() == row)
      return cell;
  }
  return nullptr;
}

void LayoutTableSection::AppendEffectiveColumn(unsigned pos) {
  DCHECK(!needs_cell_recalc_);

  for (unsigned row = 0; row < grid_.size(); ++row)
    grid_[row].row.Resize(pos + 1);
}

void LayoutTableSection::SplitEffectiveColumn(unsigned pos, unsigned first) {
  DCHECK(!needs_cell_recalc_);

  if (c_col_ > pos)
    c_col_++;
  for (unsigned row = 0; row < grid_.size(); ++row) {
    Row& r = grid_[row].row;
    EnsureCols(row, pos + 2);
    r.insert(pos + 1, CellStruct());
    if (r[pos].HasCells()) {
      r[pos + 1].cells.AppendVector(r[pos].cells);
      LayoutTableCell* cell = r[pos].PrimaryCell();
      DCHECK(cell);
      DCHECK_GE(cell->ColSpan(), (r[pos].in_col_span ? 1u : 0));
      unsigned colleft = cell->ColSpan() - r[pos].in_col_span;
      if (first > colleft)
        r[pos + 1].in_col_span = 0;
      else
        r[pos + 1].in_col_span = first + r[pos].in_col_span;
    } else {
      r[pos + 1].in_col_span = 0;
    }
  }
}

// Hit Testing
bool LayoutTableSection::NodeAtPoint(
    HitTestResult& result,
    const HitTestLocation& location_in_container,
    const LayoutPoint& accumulated_offset,
    HitTestAction action) {
  // If we have no children then we have nothing to do.
  if (!FirstRow())
    return false;

  // Table sections cannot ever be hit tested.  Effectively they do not exist.
  // Just forward to our children always.
  LayoutPoint adjusted_location = accumulated_offset + Location();

  if (HasOverflowClip() &&
      !location_in_container.Intersects(OverflowClipRect(adjusted_location)))
    return false;

  if (HasOverflowingCell()) {
    for (LayoutTableRow* row = LastRow(); row; row = row->PreviousRow()) {
      // FIXME: We have to skip over inline flows, since they can show up inside
      // table rows at the moment (a demoted inline <form> for example). If we
      // ever implement a table-specific hit-test method (which we should do for
      // performance reasons anyway), then we can remove this check.
      if (!row->HasSelfPaintingLayer()) {
        LayoutPoint child_point =
            FlipForWritingModeForChild(row, adjusted_location);
        if (row->NodeAtPoint(result, location_in_container, child_point,
                             action)) {
          UpdateHitTestResult(
              result,
              ToLayoutPoint(location_in_container.Point() - child_point));
          return true;
        }
      }
    }
    return false;
  }

  RecalcCellsIfNeeded();

  LayoutRect hit_test_rect = LayoutRect(location_in_container.BoundingBox());
  hit_test_rect.MoveBy(-adjusted_location);

  LayoutRect table_aligned_rect =
      LogicalRectForWritingModeAndDirection(hit_test_rect);
  CellSpan row_span = SpannedRows(table_aligned_rect);
  CellSpan column_span = SpannedEffectiveColumns(table_aligned_rect);

  // Now iterate over the spanned rows and columns.
  for (unsigned hit_row = row_span.Start(); hit_row < row_span.end();
       ++hit_row) {
    unsigned n_cols = NumCols(hit_row);
    for (unsigned hit_column = column_span.Start();
         hit_column < n_cols && hit_column < column_span.end(); ++hit_column) {
      CellStruct& current = CellAt(hit_row, hit_column);

      // If the cell is empty, there's nothing to do
      if (!current.HasCells())
        continue;

      for (unsigned i = current.cells.size(); i;) {
        --i;
        LayoutTableCell* cell = current.cells[i];
        LayoutPoint cell_point =
            FlipForWritingModeForChild(cell, adjusted_location);
        if (static_cast<LayoutObject*>(cell)->NodeAtPoint(
                result, location_in_container, cell_point, action)) {
          UpdateHitTestResult(
              result, location_in_container.Point() - ToLayoutSize(cell_point));
          return true;
        }
      }
      if (!result.GetHitTestRequest().ListBased())
        break;
    }
    if (!result.GetHitTestRequest().ListBased())
      break;
  }

  return false;
}

LayoutTableSection* LayoutTableSection::CreateAnonymousWithParent(
    const LayoutObject* parent) {
  RefPtr<ComputedStyle> new_style =
      ComputedStyle::CreateAnonymousStyleWithDisplay(parent->StyleRef(),
                                                     EDisplay::kTableRowGroup);
  LayoutTableSection* new_section = new LayoutTableSection(nullptr);
  new_section->SetDocumentForAnonymous(&parent->GetDocument());
  new_section->SetStyle(std::move(new_style));
  return new_section;
}

void LayoutTableSection::SetLogicalPositionForCell(
    LayoutTableCell* cell,
    unsigned effective_column) const {
  LayoutPoint cell_location(0, row_pos_[cell->RowIndex()]);
  int horizontal_border_spacing = Table()->HBorderSpacing();

  // FIXME: The table's direction should determine our row's direction, not the
  // section's (see bug 96691).
  if (!Style()->IsLeftToRightDirection())
    cell_location.SetX(LayoutUnit(
        Table()->EffectiveColumnPositions()[Table()->NumEffectiveColumns()] -
        Table()->EffectiveColumnPositions()
            [Table()->AbsoluteColumnToEffectiveColumn(
                cell->AbsoluteColumnIndex() + cell->ColSpan())] +
        horizontal_border_spacing));
  else
    cell_location.SetX(
        LayoutUnit(Table()->EffectiveColumnPositions()[effective_column] +
                   horizontal_border_spacing));

  cell->SetLogicalLocation(cell_location);
}

void LayoutTableSection::RelayoutCellIfFlexed(LayoutTableCell& cell,
                                              int row_index,
                                              int row_height) {
  // Force percent height children to lay themselves out again.
  // This will cause these children to grow to fill the cell.
  // FIXME: There is still more work to do here to fully match WinIE (should
  // it become necessary to do so).  In quirks mode, WinIE behaves like we
  // do, but it will clip the cells that spill out of the table section.
  // strict mode, Mozilla and WinIE both regrow the table to accommodate the
  // new height of the cell (thus letting the percentages cause growth one
  // time only). We may also not be handling row-spanning cells correctly.
  //
  // Note also the oddity where replaced elements always flex, and yet blocks/
  // tables do not necessarily flex. WinIE is crazy and inconsistent, and we
  // can't hope to match the behavior perfectly, but we'll continue to refine it
  // as we discover new bugs. :)
  bool cell_children_flex = false;
  bool flex_all_children = cell.Style()->LogicalHeight().IsSpecified() ||
                           (!Table()->Style()->LogicalHeight().IsAuto() &&
                            row_height != cell.LogicalHeight());

  for (LayoutObject* child = cell.FirstChild(); child;
       child = child->NextSibling()) {
    if (!child->IsText() && child->Style()->LogicalHeight().IsPercentOrCalc() &&
        (flex_all_children || ShouldFlexCellChild(cell, child)) &&
        (!child->IsTable() || ToLayoutTable(child)->HasSections())) {
      cell_children_flex = true;
      break;
    }
  }

  if (!cell_children_flex) {
    if (TrackedLayoutBoxListHashSet* percent_height_descendants =
            cell.PercentHeightDescendants()) {
      for (auto* descendant : *percent_height_descendants) {
        if (flex_all_children || ShouldFlexCellChild(cell, descendant)) {
          cell_children_flex = true;
          break;
        }
      }
    }
  }

  if (!cell_children_flex)
    return;

  // Alignment within a cell is based off the calculated height, which becomes
  // irrelevant once the cell has been resized based off its percentage.
  cell.SetOverrideLogicalContentHeightFromRowHeight(LayoutUnit(row_height));
  cell.ForceChildLayout();

  // If the baseline moved, we may have to update the data for our row. Find
  // out the new baseline.
  if (cell.IsBaselineAligned()) {
    int baseline = cell.CellBaselinePosition();
    if (baseline > cell.BorderBefore() + cell.PaddingBefore())
      grid_[row_index].baseline = std::max(grid_[row_index].baseline, baseline);
  }
}

int LayoutTableSection::LogicalHeightForRow(
    const LayoutTableRow& row_object) const {
  unsigned row_index = row_object.RowIndex();
  DCHECK_LT(row_index, grid_.size());
  int logical_height = 0;
  const Row& row = grid_[row_index].row;
  unsigned cols = row.size();
  for (unsigned col_index = 0; col_index < cols; col_index++) {
    const CellStruct& cell_struct = CellAt(row_index, col_index);
    const LayoutTableCell* cell = cell_struct.PrimaryCell();
    if (!cell || cell_struct.in_col_span)
      continue;
    unsigned row_span = cell->RowSpan();
    if (row_span == 1) {
      logical_height =
          std::max(logical_height, cell->LogicalHeightForRowSizing());
      continue;
    }
    unsigned row_index_for_cell = cell->RowIndex();
    if (row_index == grid_.size() - 1 ||
        (row_span > 1 && row_index - row_index_for_cell == row_span - 1)) {
      // This is the last row of the rowspanned cell. Add extra height if
      // needed.
      if (LayoutTableRow* first_row_for_cell =
              grid_[row_index_for_cell].row_layout_object) {
        int min_logical_height = cell->LogicalHeightForRowSizing();
        // Subtract space provided by previous rows.
        min_logical_height -= row_object.LogicalTop().ToInt() -
                              first_row_for_cell->LogicalTop().ToInt();

        logical_height = std::max(logical_height, min_logical_height);
      }
    }
  }

  if (grid_[row_index].logical_height.IsSpecified()) {
    LayoutUnit specified_logical_height =
        MinimumValueForLength(grid_[row_index].logical_height, LayoutUnit());
    logical_height = std::max(logical_height, specified_logical_height.ToInt());
  }
  return logical_height;
}

void LayoutTableSection::AdjustRowForPagination(LayoutTableRow& row_object,
                                                SubtreeLayoutScope& layouter) {
  row_object.SetPaginationStrut(LayoutUnit());
  row_object.SetLogicalHeight(LayoutUnit(LogicalHeightForRow(row_object)));
  int pagination_strut =
      PaginationStrutForRow(&row_object, row_object.LogicalTop());
  bool row_is_at_top_of_column = false;
  LayoutUnit offset_from_top_of_page;
  if (!pagination_strut) {
    LayoutUnit page_logical_height =
        PageLogicalHeightForOffset(row_object.LogicalTop());
    if (page_logical_height && Table()->Header() &&
        Table()->RowOffsetFromRepeatingHeader()) {
      offset_from_top_of_page =
          page_logical_height -
          PageRemainingLogicalHeightForOffset(row_object.LogicalTop(),
                                              kAssociateWithLatterPage);
      row_is_at_top_of_column =
          !offset_from_top_of_page ||
          offset_from_top_of_page <= Table()->VBorderSpacing();
    }

    if (!row_is_at_top_of_column)
      return;
  }
  // We need to push this row to the next fragmentainer. If there are repeated
  // table headers, we need to make room for those at the top of the next
  // fragmentainer, above this row. Otherwise, this row will just go at the top
  // of the next fragmentainer.

  LayoutTableSection* header = Table()->Header();
  if (row_object.IsFirstRowInSectionAfterHeader())
    Table()->SetRowOffsetFromRepeatingHeader(LayoutUnit());
  // Border spacing from the previous row has pushed this row just past the top
  // of the page, so we must reposition it to the top of the page and avoid any
  // repeating header.
  if (row_is_at_top_of_column && offset_from_top_of_page)
    pagination_strut -= offset_from_top_of_page.ToInt();

  // If we have a header group we will paint it at the top of each page,
  // move the rows down to accomodate it.
  if (header && header != this)
    pagination_strut += Table()->RowOffsetFromRepeatingHeader().ToInt();
  row_object.SetPaginationStrut(LayoutUnit(pagination_strut));

  // We have inserted a pagination strut before the row. Adjust the logical top
  // and re-lay out. We no longer want to break inside the row, but rather
  // *before* it. From the previous layout pass, there are most likely
  // pagination struts inside some cell in this row that we need to get rid of.
  row_object.SetLogicalTop(row_object.LogicalTop() + pagination_strut);
  layouter.SetChildNeedsLayout(&row_object);
  row_object.LayoutIfNeeded();

  // It's very likely that re-laying out (and nuking pagination struts inside
  // cells) gave us a new height.
  row_object.SetLogicalHeight(LayoutUnit(LogicalHeightForRow(row_object)));
}

bool LayoutTableSection::IsRepeatingHeaderGroup() const {
  if (GetPaginationBreakability() == LayoutBox::kAllowAnyBreaks)
    return false;
  // TODO(rhogan): Should we paint a header repeatedly if it's self-painting?
  if (HasSelfPaintingLayer())
    return false;
  LayoutUnit page_height = Table()->PageLogicalHeightForOffset(LayoutUnit());
  if (!page_height)
    return false;

  if (LogicalHeight() > page_height)
    return false;

  // If the first row of the section after the header group doesn't fit on the
  // page, then don't repeat the header on each page.
  // See https://drafts.csswg.org/css-tables-3/#repeated-headers
  LayoutTableSection* section_below = Table()->SectionBelow(this);
  if (!section_below)
    return true;
  if (LayoutTableRow* first_row = section_below->FirstRow()) {
    if (first_row->PaginationStrut() ||
        first_row->LogicalHeight() > page_height)
      return false;
  }

  return true;
}

bool LayoutTableSection::MapToVisualRectInAncestorSpaceInternal(
    const LayoutBoxModelObject* ancestor,
    TransformState& transform_state,
    VisualRectFlags flags) const {
  if (ancestor == this)
    return true;
  // Repeating table headers are painted once per fragmentation page/column.
  // This does not go through the regular fragmentation machinery, so we need
  // special code to expand the invalidation rect to contain all positions of
  // the header in all columns.
  // Note that this is in flow thread coordinates, not visual coordinates. The
  // enclosing LayoutFlowThread will convert to visual coordinates.
  if (Table()->Header() == this && IsRepeatingHeaderGroup()) {
    transform_state.Flatten();
    FloatRect rect = transform_state.LastPlanarQuad().BoundingBox();
    rect.SetHeight(Table()->LogicalHeight());
    transform_state.SetQuad(FloatQuad(rect));
  }
  return LayoutTableBoxComponent::MapToVisualRectInAncestorSpaceInternal(
      ancestor, transform_state, flags);
}

bool LayoutTableSection::PaintedOutputOfObjectHasNoEffectRegardlessOfSize()
    const {
  // LayoutTableSection paints background from columns.
  if (Table()->HasColElements())
    return false;
  return LayoutTableBoxComponent::
      PaintedOutputOfObjectHasNoEffectRegardlessOfSize();
}

}  // namespace blink
