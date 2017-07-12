/*
 * Copyright (C) 1999 Lars Knoll (knoll@kde.org)
 *           (C) 1999 Antti Koivisto (koivisto@kde.org)
 *           (C) 2001 Dirk Mueller (mueller@kde.org)
 * Copyright (C) 2004, 2005, 2006, 2007, 2008, 2009, 2010, 2011, 2012 Apple Inc.
 * All rights reserved.
 * Copyright (C) 2008, 2009 Torch Mobile Inc. All rights reserved.
 * (http://www.torchmobile.com/)
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
 *
 */

#include "core/dom/NodeTraversal.h"

#include "core/dom/ContainerNode.h"
#include "core/dom/Range.h"

namespace blink {

Node* NodeTraversal::PreviousIncludingPseudo(const Node& current,
                                             const Node* stay_within) {
  if (current == stay_within)
    return 0;
  if (Node* previous = current.PseudoAwarePreviousSibling()) {
    while (previous->PseudoAwareLastChild())
      previous = previous->PseudoAwareLastChild();
    return previous;
  }
  return current.parentNode();
}

Node* NodeTraversal::NextIncludingPseudo(const Node& current,
                                         const Node* stay_within) {
  if (Node* next = current.PseudoAwareFirstChild())
    return next;
  if (current == stay_within)
    return 0;
  if (Node* next = current.PseudoAwareNextSibling())
    return next;
  for (Node& parent : AncestorsOf(current)) {
    if (parent == stay_within)
      return 0;
    if (Node* next = parent.PseudoAwareNextSibling())
      return next;
  }
  return 0;
}

Node* NodeTraversal::NextIncludingPseudoSkippingChildren(
    const Node& current,
    const Node* stay_within) {
  if (current == stay_within)
    return 0;
  if (Node* next = current.PseudoAwareNextSibling())
    return next;
  for (Node& parent : AncestorsOf(current)) {
    if (parent == stay_within)
      return 0;
    if (Node* next = parent.PseudoAwareNextSibling())
      return next;
  }
  return 0;
}

Node* NodeTraversal::NextAncestorSibling(const Node& current) {
  DCHECK(!current.nextSibling());
  for (Node& parent : AncestorsOf(current)) {
    if (parent.nextSibling())
      return parent.nextSibling();
  }
  return 0;
}

Node* NodeTraversal::NextAncestorSibling(const Node& current,
                                         const Node* stay_within) {
  DCHECK(!current.nextSibling());
  DCHECK_NE(current, stay_within);
  for (Node& parent : AncestorsOf(current)) {
    if (parent == stay_within)
      return 0;
    if (parent.nextSibling())
      return parent.nextSibling();
  }
  return 0;
}

Node* NodeTraversal::LastWithin(const ContainerNode& current) {
  Node* descendant = current.LastChild();
  for (Node* child = descendant; child; child = child->lastChild())
    descendant = child;
  return descendant;
}

Node& NodeTraversal::LastWithinOrSelf(Node& current) {
  Node* last_descendant =
      current.IsContainerNode()
          ? NodeTraversal::LastWithin(ToContainerNode(current))
          : 0;
  return last_descendant ? *last_descendant : current;
}

Node* NodeTraversal::Previous(const Node& current, const Node* stay_within) {
  if (current == stay_within)
    return 0;
  if (current.previousSibling()) {
    Node* previous = current.previousSibling();
    while (Node* child = previous->lastChild())
      previous = child;
    return previous;
  }
  return current.parentNode();
}

Node* NodeTraversal::PreviousSkippingChildren(const Node& current,
                                              const Node* stay_within) {
  if (current == stay_within)
    return 0;
  if (current.previousSibling())
    return current.previousSibling();
  for (Node& parent : AncestorsOf(current)) {
    if (parent == stay_within)
      return 0;
    if (parent.previousSibling())
      return parent.previousSibling();
  }
  return 0;
}

Node* NodeTraversal::NextPostOrder(const Node& current,
                                   const Node* stay_within) {
  if (current == stay_within)
    return 0;
  if (!current.nextSibling())
    return current.parentNode();
  Node* next = current.nextSibling();
  while (Node* child = next->firstChild())
    next = child;
  return next;
}

Node* NodeTraversal::PreviousAncestorSiblingPostOrder(const Node& current,
                                                      const Node* stay_within) {
  DCHECK(!current.previousSibling());
  for (Node& parent : NodeTraversal::AncestorsOf(current)) {
    if (parent == stay_within)
      return 0;
    if (parent.previousSibling())
      return parent.previousSibling();
  }
  return 0;
}

Node* NodeTraversal::PreviousPostOrder(const Node& current,
                                       const Node* stay_within) {
  if (Node* last_child = current.lastChild())
    return last_child;
  if (current == stay_within)
    return 0;
  if (current.previousSibling())
    return current.previousSibling();
  return PreviousAncestorSiblingPostOrder(current, stay_within);
}

Node* NodeTraversal::CommonAncestor(const Node& node_a, const Node& node_b) {
  return Range::commonAncestorContainer(&node_a, &node_b);
}

}  // namespace blink
