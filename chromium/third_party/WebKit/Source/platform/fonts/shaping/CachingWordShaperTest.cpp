// Copyright (c) 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform/fonts/shaping/CachingWordShaper.h"

#include <memory>
#include "platform/fonts/CharacterRange.h"
#include "platform/fonts/FontCache.h"
#include "platform/fonts/shaping/CachingWordShapeIterator.h"
#include "platform/fonts/shaping/ShapeResultTestInfo.h"
#include "platform/wtf/PtrUtil.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace blink {

class CachingWordShaperTest : public ::testing::Test {
 protected:
  void SetUp() override {
    font_description.SetComputedSize(12.0);
    font_description.SetLocale(LayoutLocale::Get("en"));
    ASSERT_EQ(USCRIPT_LATIN, font_description.GetScript());
    font_description.SetGenericFamily(FontDescription::kStandardFamily);

    font = Font(font_description);
    font.Update(nullptr);
    ASSERT_TRUE(font.CanShapeWordByWord());
    fallback_fonts = nullptr;
    cache = WTF::MakeUnique<ShapeCache>();
  }

  FontCachePurgePreventer font_cache_purge_preventer;
  FontDescription font_description;
  Font font;
  std::unique_ptr<ShapeCache> cache;
  HashSet<const SimpleFontData*>* fallback_fonts;
  unsigned start_index = 0;
  unsigned num_glyphs = 0;
  hb_script_t script = HB_SCRIPT_INVALID;
};

static inline const ShapeResultTestInfo* TestInfo(
    RefPtr<const ShapeResult>& result) {
  return static_cast<const ShapeResultTestInfo*>(result.Get());
}

TEST_F(CachingWordShaperTest, LatinLeftToRightByWord) {
  TextRun text_run(reinterpret_cast<const LChar*>("ABC DEF."), 8);

  RefPtr<const ShapeResult> result;
  CachingWordShapeIterator iterator(cache.get(), text_run, &font);
  ASSERT_TRUE(iterator.Next(&result));
  ASSERT_TRUE(
      TestInfo(result)->RunInfoForTesting(0, start_index, num_glyphs, script));
  EXPECT_EQ(0u, start_index);
  EXPECT_EQ(3u, num_glyphs);
  EXPECT_EQ(HB_SCRIPT_LATIN, script);

  ASSERT_TRUE(iterator.Next(&result));
  ASSERT_TRUE(
      TestInfo(result)->RunInfoForTesting(0, start_index, num_glyphs, script));
  EXPECT_EQ(0u, start_index);
  EXPECT_EQ(1u, num_glyphs);
  EXPECT_EQ(HB_SCRIPT_COMMON, script);

  ASSERT_TRUE(iterator.Next(&result));
  ASSERT_TRUE(
      TestInfo(result)->RunInfoForTesting(0, start_index, num_glyphs, script));
  EXPECT_EQ(0u, start_index);
  EXPECT_EQ(4u, num_glyphs);
  EXPECT_EQ(HB_SCRIPT_LATIN, script);

  ASSERT_FALSE(iterator.Next(&result));
}

TEST_F(CachingWordShaperTest, CommonAccentLeftToRightByWord) {
  const UChar kStr[] = {0x2F, 0x301, 0x2E, 0x20, 0x2E, 0x0};
  TextRun text_run(kStr, 5);

  unsigned offset = 0;
  RefPtr<const ShapeResult> result;
  CachingWordShapeIterator iterator(cache.get(), text_run, &font);
  ASSERT_TRUE(iterator.Next(&result));
  ASSERT_TRUE(
      TestInfo(result)->RunInfoForTesting(0, start_index, num_glyphs, script));
  EXPECT_EQ(0u, offset + start_index);
  EXPECT_EQ(3u, num_glyphs);
  EXPECT_EQ(HB_SCRIPT_COMMON, script);
  offset += result->NumCharacters();

  ASSERT_TRUE(iterator.Next(&result));
  ASSERT_TRUE(
      TestInfo(result)->RunInfoForTesting(0, start_index, num_glyphs, script));
  EXPECT_EQ(3u, offset + start_index);
  EXPECT_EQ(1u, num_glyphs);
  EXPECT_EQ(HB_SCRIPT_COMMON, script);
  offset += result->NumCharacters();

  ASSERT_TRUE(iterator.Next(&result));
  ASSERT_TRUE(
      TestInfo(result)->RunInfoForTesting(0, start_index, num_glyphs, script));
  EXPECT_EQ(4u, offset + start_index);
  EXPECT_EQ(1u, num_glyphs);
  EXPECT_EQ(HB_SCRIPT_COMMON, script);
  offset += result->NumCharacters();

  ASSERT_EQ(5u, offset);
  ASSERT_FALSE(iterator.Next(&result));
}

// Tests that filling a glyph buffer for a specific range returns the same
// results when shaping word by word as when shaping the full run in one go.
TEST_F(CachingWordShaperTest, CommonAccentLeftToRightFillGlyphBuffer) {
  // "/. ." with an accent mark over the first dot.
  const UChar kStr[] = {0x2F, 0x301, 0x2E, 0x20, 0x2E, 0x0};
  TextRun text_run(kStr, 5);
  TextRunPaintInfo run_info(text_run);
  run_info.to = 3;

  ShapeResultBloberizer bloberizer(font, 1);
  CachingWordShaper(font).FillGlyphs(run_info, bloberizer);

  Font reference_font(font_description);
  reference_font.Update(nullptr);
  reference_font.SetCanShapeWordByWordForTesting(false);
  ShapeResultBloberizer reference_bloberizer(reference_font, 1);
  CachingWordShaper(reference_font).FillGlyphs(run_info, reference_bloberizer);

  const auto& glyphs =
      ShapeResultBloberizerTestInfo::PendingRunGlyphs(bloberizer);
  ASSERT_EQ(glyphs.size(), 3ul);
  const auto reference_glyphs =
      ShapeResultBloberizerTestInfo::PendingRunGlyphs(reference_bloberizer);
  ASSERT_EQ(reference_glyphs.size(), 3ul);

  EXPECT_EQ(reference_glyphs[0], glyphs[0]);
  EXPECT_EQ(reference_glyphs[1], glyphs[1]);
  EXPECT_EQ(reference_glyphs[2], glyphs[2]);
}

// Tests that filling a glyph buffer for a specific range returns the same
// results when shaping word by word as when shaping the full run in one go.
TEST_F(CachingWordShaperTest, CommonAccentRightToLeftFillGlyphBuffer) {
  // "[] []" with an accent mark over the last square bracket.
  const UChar kStr[] = {0x5B, 0x5D, 0x20, 0x5B, 0x301, 0x5D, 0x0};
  TextRun text_run(kStr, 6);
  text_run.SetDirection(TextDirection::kRtl);
  TextRunPaintInfo run_info(text_run);
  run_info.from = 1;

  ShapeResultBloberizer bloberizer(font, 1);
  CachingWordShaper(font).FillGlyphs(run_info, bloberizer);

  Font reference_font(font_description);
  reference_font.Update(nullptr);
  reference_font.SetCanShapeWordByWordForTesting(false);
  ShapeResultBloberizer reference_bloberizer(reference_font, 1);
  CachingWordShaper(reference_font).FillGlyphs(run_info, reference_bloberizer);

  const auto& glyphs =
      ShapeResultBloberizerTestInfo::PendingRunGlyphs(bloberizer);
  ASSERT_EQ(5u, glyphs.size());
  const auto reference_glyphs =
      ShapeResultBloberizerTestInfo::PendingRunGlyphs(reference_bloberizer);
  ASSERT_EQ(5u, reference_glyphs.size());

  EXPECT_EQ(reference_glyphs[0], glyphs[0]);
  EXPECT_EQ(reference_glyphs[1], glyphs[1]);
  EXPECT_EQ(reference_glyphs[2], glyphs[2]);
  EXPECT_EQ(reference_glyphs[3], glyphs[3]);
  EXPECT_EQ(reference_glyphs[4], glyphs[4]);
}

// Tests that runs with zero glyphs (the ZWJ non-printable character in this
// case) are handled correctly. This test passes if it does not cause a crash.
TEST_F(CachingWordShaperTest, SubRunWithZeroGlyphs) {
  // "Foo &zwnj; bar"
  const UChar kStr[] = {0x46, 0x6F, 0x6F, 0x20, 0x200C,
                        0x20, 0x62, 0x61, 0x71, 0x0};
  TextRun text_run(kStr, 9);

  CachingWordShaper shaper(font);
  FloatRect glyph_bounds;
  ASSERT_GT(shaper.Width(text_run, nullptr, &glyph_bounds), 0);

  ShapeResultBloberizer bloberizer(font, 1);
  TextRunPaintInfo run_info(text_run);
  run_info.to = 8;
  shaper.FillGlyphs(run_info, bloberizer);

  shaper.GetCharacterRange(text_run, 0, 8);
}

TEST_F(CachingWordShaperTest, SegmentCJKByCharacter) {
  const UChar kStr[] = {0x56FD, 0x56FD,  // CJK Unified Ideograph
                        'a',    'b',
                        0x56FD,  // CJK Unified Ideograph
                        'x',    'y',    'z',
                        0x3042,  // HIRAGANA LETTER A
                        0x56FD,  // CJK Unified Ideograph
                        0x0};
  TextRun text_run(kStr, 10);

  RefPtr<const ShapeResult> word_result;
  CachingWordShapeIterator iterator(cache.get(), text_run, &font);

  ASSERT_TRUE(iterator.Next(&word_result));
  EXPECT_EQ(1u, word_result->NumCharacters());
  ASSERT_TRUE(iterator.Next(&word_result));
  EXPECT_EQ(1u, word_result->NumCharacters());

  ASSERT_TRUE(iterator.Next(&word_result));
  EXPECT_EQ(2u, word_result->NumCharacters());

  ASSERT_TRUE(iterator.Next(&word_result));
  EXPECT_EQ(1u, word_result->NumCharacters());

  ASSERT_TRUE(iterator.Next(&word_result));
  EXPECT_EQ(3u, word_result->NumCharacters());

  ASSERT_TRUE(iterator.Next(&word_result));
  EXPECT_EQ(1u, word_result->NumCharacters());
  ASSERT_TRUE(iterator.Next(&word_result));
  EXPECT_EQ(1u, word_result->NumCharacters());

  ASSERT_FALSE(iterator.Next(&word_result));
}

TEST_F(CachingWordShaperTest, SegmentCJKAndCommon) {
  const UChar kStr[] = {'a',    'b',
                        0xFF08,  // FULLWIDTH LEFT PARENTHESIS (script=common)
                        0x56FD,  // CJK Unified Ideograph
                        0x56FD,  // CJK Unified Ideograph
                        0x56FD,  // CJK Unified Ideograph
                        0x3002,  // IDEOGRAPHIC FULL STOP (script=common)
                        0x0};
  TextRun text_run(kStr, 7);

  RefPtr<const ShapeResult> word_result;
  CachingWordShapeIterator iterator(cache.get(), text_run, &font);

  ASSERT_TRUE(iterator.Next(&word_result));
  EXPECT_EQ(2u, word_result->NumCharacters());

  ASSERT_TRUE(iterator.Next(&word_result));
  EXPECT_EQ(2u, word_result->NumCharacters());

  ASSERT_TRUE(iterator.Next(&word_result));
  EXPECT_EQ(1u, word_result->NumCharacters());

  ASSERT_TRUE(iterator.Next(&word_result));
  EXPECT_EQ(2u, word_result->NumCharacters());

  ASSERT_FALSE(iterator.Next(&word_result));
}

TEST_F(CachingWordShaperTest, SegmentCJKAndInherit) {
  const UChar kStr[] = {
      0x304B,  // HIRAGANA LETTER KA
      0x304B,  // HIRAGANA LETTER KA
      0x3009,  // COMBINING KATAKANA-HIRAGANA VOICED SOUND MARK
      0x304B,  // HIRAGANA LETTER KA
      0x0};
  TextRun text_run(kStr, 4);

  RefPtr<const ShapeResult> word_result;
  CachingWordShapeIterator iterator(cache.get(), text_run, &font);

  ASSERT_TRUE(iterator.Next(&word_result));
  EXPECT_EQ(1u, word_result->NumCharacters());

  ASSERT_TRUE(iterator.Next(&word_result));
  EXPECT_EQ(2u, word_result->NumCharacters());

  ASSERT_TRUE(iterator.Next(&word_result));
  EXPECT_EQ(1u, word_result->NumCharacters());

  ASSERT_FALSE(iterator.Next(&word_result));
}

TEST_F(CachingWordShaperTest, SegmentCJKAndNonCJKCommon) {
  const UChar kStr[] = {0x56FD,  // CJK Unified Ideograph
                        ' ', 0x0};
  TextRun text_run(kStr, 2);

  RefPtr<const ShapeResult> word_result;
  CachingWordShapeIterator iterator(cache.get(), text_run, &font);

  ASSERT_TRUE(iterator.Next(&word_result));
  EXPECT_EQ(1u, word_result->NumCharacters());

  ASSERT_TRUE(iterator.Next(&word_result));
  EXPECT_EQ(1u, word_result->NumCharacters());

  ASSERT_FALSE(iterator.Next(&word_result));
}

TEST_F(CachingWordShaperTest, SegmentEmojiZWJCommon) {
  // A family followed by a couple with heart emoji sequence,
  // the latter including a variation selector.
  const UChar kStr[] = {0xD83D, 0xDC68, 0x200D, 0xD83D, 0xDC69, 0x200D,
                        0xD83D, 0xDC67, 0x200D, 0xD83D, 0xDC66, 0xD83D,
                        0xDC69, 0x200D, 0x2764, 0xFE0F, 0x200D, 0xD83D,
                        0xDC8B, 0x200D, 0xD83D, 0xDC68, 0x0};
  TextRun text_run(kStr, 22);

  RefPtr<const ShapeResult> word_result;
  CachingWordShapeIterator iterator(cache.get(), text_run, &font);

  ASSERT_TRUE(iterator.Next(&word_result));
  EXPECT_EQ(22u, word_result->NumCharacters());

  ASSERT_FALSE(iterator.Next(&word_result));
}

TEST_F(CachingWordShaperTest, SegmentEmojiPilotJudgeSequence) {
  // A family followed by a couple with heart emoji sequence,
  // the latter including a variation selector.
  const UChar kStr[] = {0xD83D, 0xDC68, 0xD83C, 0xDFFB, 0x200D, 0x2696, 0xFE0F,
                        0xD83D, 0xDC68, 0xD83C, 0xDFFB, 0x200D, 0x2708, 0xFE0F};
  TextRun text_run(kStr, ARRAY_SIZE(kStr));

  RefPtr<const ShapeResult> word_result;
  CachingWordShapeIterator iterator(cache.get(), text_run, &font);

  ASSERT_TRUE(iterator.Next(&word_result));
  EXPECT_EQ(ARRAY_SIZE(kStr), word_result->NumCharacters());

  ASSERT_FALSE(iterator.Next(&word_result));
}

TEST_F(CachingWordShaperTest, SegmentEmojiHeartZWJSequence) {
  // A ZWJ, followed by two family ZWJ Sequences.
  const UChar kStr[] = {0xD83D, 0xDC69, 0x200D, 0x2764, 0xFE0F, 0x200D,
                        0xD83D, 0xDC8B, 0x200D, 0xD83D, 0xDC68, 0x0};
  TextRun text_run(kStr, 11);

  RefPtr<const ShapeResult> word_result;
  CachingWordShapeIterator iterator(cache.get(), text_run, &font);

  ASSERT_TRUE(iterator.Next(&word_result));
  EXPECT_EQ(11u, word_result->NumCharacters());

  ASSERT_FALSE(iterator.Next(&word_result));
}

TEST_F(CachingWordShaperTest, SegmentEmojiSignsOfHornsModifier) {
  // A Sign of the Horns emoji, followed by a fitzpatrick modifer
  const UChar kStr[] = {0xD83E, 0xDD18, 0xD83C, 0xDFFB, 0x0};
  TextRun text_run(kStr, 4);

  RefPtr<const ShapeResult> word_result;
  CachingWordShapeIterator iterator(cache.get(), text_run, &font);

  ASSERT_TRUE(iterator.Next(&word_result));
  EXPECT_EQ(4u, word_result->NumCharacters());

  ASSERT_FALSE(iterator.Next(&word_result));
}

TEST_F(CachingWordShaperTest, SegmentEmojiExtraZWJPrefix) {
  // A ZWJ, followed by a family and a heart-kiss sequence.
  const UChar kStr[] = {0x200D, 0xD83D, 0xDC68, 0x200D, 0xD83D, 0xDC69,
                        0x200D, 0xD83D, 0xDC67, 0x200D, 0xD83D, 0xDC66,
                        0xD83D, 0xDC69, 0x200D, 0x2764, 0xFE0F, 0x200D,
                        0xD83D, 0xDC8B, 0x200D, 0xD83D, 0xDC68, 0x0};
  TextRun text_run(kStr, 23);

  RefPtr<const ShapeResult> word_result;
  CachingWordShapeIterator iterator(cache.get(), text_run, &font);

  ASSERT_TRUE(iterator.Next(&word_result));
  EXPECT_EQ(1u, word_result->NumCharacters());

  ASSERT_TRUE(iterator.Next(&word_result));
  EXPECT_EQ(22u, word_result->NumCharacters());

  ASSERT_FALSE(iterator.Next(&word_result));
}

TEST_F(CachingWordShaperTest, SegmentCJKCommon) {
  const UChar kStr[] = {0xFF08,  // FULLWIDTH LEFT PARENTHESIS (script=common)
                        0xFF08,  // FULLWIDTH LEFT PARENTHESIS (script=common)
                        0xFF08,  // FULLWIDTH LEFT PARENTHESIS (script=common)
                        0x0};
  TextRun text_run(kStr, 3);

  RefPtr<const ShapeResult> word_result;
  CachingWordShapeIterator iterator(cache.get(), text_run, &font);

  ASSERT_TRUE(iterator.Next(&word_result));
  EXPECT_EQ(3u, word_result->NumCharacters());

  ASSERT_FALSE(iterator.Next(&word_result));
}

TEST_F(CachingWordShaperTest, SegmentCJKCommonAndNonCJK) {
  const UChar kStr[] = {0xFF08,  // FULLWIDTH LEFT PARENTHESIS (script=common)
                        'a', 'b', 0x0};
  TextRun text_run(kStr, 3);

  RefPtr<const ShapeResult> word_result;
  CachingWordShapeIterator iterator(cache.get(), text_run, &font);

  ASSERT_TRUE(iterator.Next(&word_result));
  EXPECT_EQ(1u, word_result->NumCharacters());

  ASSERT_TRUE(iterator.Next(&word_result));
  EXPECT_EQ(2u, word_result->NumCharacters());

  ASSERT_FALSE(iterator.Next(&word_result));
}

TEST_F(CachingWordShaperTest, SegmentCJKSmallFormVariants) {
  const UChar kStr[] = {0x5916,  // CJK UNIFIED IDEOGRPAH
                        0xFE50,  // SMALL COMMA
                        0x0};
  TextRun text_run(kStr, 2);

  RefPtr<const ShapeResult> word_result;
  CachingWordShapeIterator iterator(cache.get(), text_run, &font);

  ASSERT_TRUE(iterator.Next(&word_result));
  EXPECT_EQ(2u, word_result->NumCharacters());

  ASSERT_FALSE(iterator.Next(&word_result));
}

TEST_F(CachingWordShaperTest, SegmentHangulToneMark) {
  const UChar kStr[] = {0xC740,  // HANGUL SYLLABLE EUN
                        0x302E,  // HANGUL SINGLE DOT TONE MARK
                        0x0};
  TextRun text_run(kStr, 2);

  RefPtr<const ShapeResult> word_result;
  CachingWordShapeIterator iterator(cache.get(), text_run, &font);

  ASSERT_TRUE(iterator.Next(&word_result));
  EXPECT_EQ(2u, word_result->NumCharacters());

  ASSERT_FALSE(iterator.Next(&word_result));
}

TEST_F(CachingWordShaperTest, TextOrientationFallbackShouldNotInFallbackList) {
  const UChar kStr[] = {
      'A',  // code point for verticalRightOrientationFontData()
      // Ideally we'd like to test uprightOrientationFontData() too
      // using code point such as U+3042, but it'd fallback to system
      // fonts as the glyph is missing.
      0x0};
  TextRun text_run(kStr, 1);

  font_description.SetOrientation(FontOrientation::kVerticalMixed);
  Font vertical_mixed_font = Font(font_description);
  vertical_mixed_font.Update(nullptr);
  ASSERT_TRUE(vertical_mixed_font.CanShapeWordByWord());

  CachingWordShaper shaper(vertical_mixed_font);
  FloatRect glyph_bounds;
  HashSet<const SimpleFontData*> fallback_fonts;
  ASSERT_GT(shaper.Width(text_run, &fallback_fonts, &glyph_bounds), 0);
  EXPECT_EQ(0u, fallback_fonts.size());
}

TEST_F(CachingWordShaperTest, GlyphBoundsWithSpaces) {
  CachingWordShaper shaper(font);

  TextRun periods(reinterpret_cast<const LChar*>(".........."), 10);
  FloatRect periods_glyph_bounds;
  float periods_width = shaper.Width(periods, nullptr, &periods_glyph_bounds);

  TextRun periods_and_spaces(
      reinterpret_cast<const LChar*>(". . . . . . . . . ."), 19);
  FloatRect periods_and_spaces_glyph_bounds;
  float periods_and_spaces_width = shaper.Width(
      periods_and_spaces, nullptr, &periods_and_spaces_glyph_bounds);

  // The total width of periods and spaces should be longer than the width of
  // periods alone.
  ASSERT_GT(periods_and_spaces_width, periods_width);

  // The glyph bounds of periods and spaces should be longer than the glyph
  // bounds of periods alone.
  ASSERT_GT(periods_and_spaces_glyph_bounds.Width(),
            periods_glyph_bounds.Width());
}

}  // namespace blink
