/*
 * Copyright (C) 2011, 2013 Apple Inc. All rights reserved.
 * Copyright (C) 2014 Samsung Electronics. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1.  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 2.  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE AND ITS CONTRIBUTORS "AS IS" AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL APPLE OR ITS CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "core/dom/SelectorQuery.h"

#include <memory>
#include "bindings/core/v8/ExceptionState.h"
#include "core/HTMLNames.h"
#include "core/css/SelectorChecker.h"
#include "core/css/parser/CSSParser.h"
#include "core/dom/Document.h"
#include "core/dom/ElementTraversal.h"
#include "core/dom/ExceptionCode.h"
#include "core/dom/Node.h"
#include "core/dom/NthIndexCache.h"
#include "core/dom/StaticNodeList.h"
#include "core/dom/shadow/ElementShadow.h"
#include "core/dom/shadow/ShadowRoot.h"
#include "platform/wtf/PtrUtil.h"

namespace blink {

using namespace HTMLNames;

#if DCHECK_IS_ON()
static SelectorQuery::QueryStats& CurrentQueryStats() {
  DEFINE_STATIC_LOCAL(SelectorQuery::QueryStats, stats, ());
  return stats;
}

SelectorQuery::QueryStats SelectorQuery::LastQueryStats() {
  return CurrentQueryStats();
}

#define QUERY_STATS_INCREMENT(name) \
  (void)(CurrentQueryStats().total_count++, CurrentQueryStats().name++);
#define QUERY_STATS_RESET() (void)(CurrentQueryStats() = {});

#else

#define QUERY_STATS_INCREMENT(name)
#define QUERY_STATS_RESET()

#endif

struct SingleElementSelectorQueryTrait {
  typedef Element* OutputType;
  static const bool kShouldOnlyMatchFirstElement = true;
  ALWAYS_INLINE static void AppendElement(OutputType& output,
                                          Element& element) {
    DCHECK(!output);
    output = &element;
  }
};

struct AllElementsSelectorQueryTrait {
  typedef HeapVector<Member<Element>> OutputType;
  static const bool kShouldOnlyMatchFirstElement = false;
  ALWAYS_INLINE static void AppendElement(OutputType& output,
                                          Element& element) {
    output.push_back(&element);
  }
};

// TODO(esprehn): Move this to Element and update callers elsewhere.
inline bool HasClassName(const Element& element,
                         const AtomicString& class_name) {
  return element.HasClass() && element.ClassNames().Contains(class_name);
}

inline bool SelectorMatches(const CSSSelector& selector,
                            Element& element,
                            const ContainerNode& root_node) {
  SelectorChecker::Init init;
  init.mode = SelectorChecker::kQueryingRules;
  SelectorChecker checker(init);
  SelectorChecker::SelectorCheckingContext context(
      &element, SelectorChecker::kVisitedMatchDisabled);
  context.selector = &selector;
  context.scope = &root_node;
  return checker.Match(context);
}

bool SelectorQuery::Matches(Element& target_element) const {
  QUERY_STATS_RESET();
  if (needs_updated_distribution_)
    target_element.UpdateDistribution();
  return SelectorListMatches(target_element, target_element);
}

Element* SelectorQuery::Closest(Element& target_element) const {
  QUERY_STATS_RESET();
  if (selectors_.IsEmpty())
    return nullptr;
  if (needs_updated_distribution_)
    target_element.UpdateDistribution();

  for (Element* current_element = &target_element; current_element;
       current_element = current_element->parentElement()) {
    if (SelectorListMatches(target_element, *current_element))
      return current_element;
  }
  return nullptr;
}

StaticElementList* SelectorQuery::QueryAll(ContainerNode& root_node) const {
  QUERY_STATS_RESET();
  NthIndexCache nth_index_cache(root_node.GetDocument());
  HeapVector<Member<Element>> result;
  Execute<AllElementsSelectorQueryTrait>(root_node, result);
  return StaticElementList::Adopt(result);
}

Element* SelectorQuery::QueryFirst(ContainerNode& root_node) const {
  QUERY_STATS_RESET();
  NthIndexCache nth_index_cache(root_node.GetDocument());
  Element* matched_element = nullptr;
  Execute<SingleElementSelectorQueryTrait>(root_node, matched_element);
  return matched_element;
}

template <typename SelectorQueryTrait>
static void CollectElementsByClassName(
    ContainerNode& root_node,
    const AtomicString& class_name,
    const CSSSelector* selector,
    typename SelectorQueryTrait::OutputType& output) {
  for (Element& element : ElementTraversal::DescendantsOf(root_node)) {
    QUERY_STATS_INCREMENT(fast_class);
    if (!HasClassName(element, class_name))
      continue;
    if (selector && !SelectorMatches(*selector, element, root_node))
      continue;
    SelectorQueryTrait::AppendElement(output, element);
    if (SelectorQueryTrait::kShouldOnlyMatchFirstElement)
      return;
  }
}

inline bool MatchesTagName(const QualifiedName& tag_name,
                           const Element& element) {
  if (tag_name == AnyQName())
    return true;
  if (element.HasLocalName(tag_name.LocalName()))
    return true;
  // Non-html elements in html documents are normalized to their camel-cased
  // version during parsing if applicable. Yet, type selectors are lower-cased
  // for selectors in html documents. Compare the upper case converted names
  // instead to allow matching SVG elements like foreignObject.
  if (!element.IsHTMLElement() && element.GetDocument().IsHTMLDocument())
    return element.TagQName().LocalNameUpper() == tag_name.LocalNameUpper();
  return false;
}

template <typename SelectorQueryTrait>
static void CollectElementsByTagName(
    ContainerNode& root_node,
    const QualifiedName& tag_name,
    typename SelectorQueryTrait::OutputType& output) {
  DCHECK_EQ(tag_name.NamespaceURI(), g_star_atom);
  for (Element& element : ElementTraversal::DescendantsOf(root_node)) {
    QUERY_STATS_INCREMENT(fast_tag_name);
    if (MatchesTagName(tag_name, element)) {
      SelectorQueryTrait::AppendElement(output, element);
      if (SelectorQueryTrait::kShouldOnlyMatchFirstElement)
        return;
    }
  }
}

inline bool SelectorQuery::CanUseFastQuery(
    const ContainerNode& root_node) const {
  if (uses_deep_combinator_or_shadow_pseudo_)
    return false;
  if (needs_updated_distribution_)
    return false;
  if (root_node.GetDocument().InQuirksMode())
    return false;
  if (!root_node.isConnected())
    return false;
  return selectors_.size() == 1;
}

inline bool AncestorHasClassName(ContainerNode& root_node,
                                 const AtomicString& class_name) {
  if (!root_node.IsElementNode())
    return false;

  for (Element* element = &ToElement(root_node); element;
       element = element->parentElement()) {
    if (HasClassName(*element, class_name))
      return true;
  }
  return false;
}

template <typename SelectorQueryTrait>
void SelectorQuery::FindTraverseRootsAndExecute(
    ContainerNode& root_node,
    typename SelectorQueryTrait::OutputType& output) const {
  // We need to return the matches in document order. To use id lookup while
  // there is possiblity of multiple matches we would need to sort the
  // results. For now, just traverse the document in that case.
  DCHECK_EQ(selectors_.size(), 1u);

  bool is_rightmost_selector = true;
  bool start_from_parent = false;

  for (const CSSSelector* selector = selectors_[0]; selector;
       selector = selector->TagHistory()) {
    if (selector->Match() == CSSSelector::kId &&
        !root_node.ContainingTreeScope().ContainsMultipleElementsWithId(
            selector->Value())) {
      // Id selectors in the right most selector are handled by the caller,
      // we should never hit them here.
      DCHECK(!is_rightmost_selector);
      Element* element =
          root_node.ContainingTreeScope().GetElementById(selector->Value());
      if (!element)
        return;
      ContainerNode* start = &root_node;
      if (element->IsDescendantOf(&root_node))
        start = element;
      if (start_from_parent)
        start = start->parentNode();
      if (!start)
        return;
      ExecuteForTraverseRoot<SelectorQueryTrait>(*start, root_node, output);
      return;
    }

    // If we have both CSSSelector::Id and CSSSelector::Class at the same time,
    // we should use Id to find traverse root.
    if (!SelectorQueryTrait::kShouldOnlyMatchFirstElement &&
        !start_from_parent && selector->Match() == CSSSelector::kClass) {
      if (is_rightmost_selector) {
        CollectElementsByClassName<SelectorQueryTrait>(
            root_node, selector->Value(), selectors_[0], output);
        return;
      }
      // Since there exists some ancestor element which has the class name, we
      // need to see all children of rootNode.
      if (AncestorHasClassName(root_node, selector->Value()))
        break;

      const AtomicString& class_name = selector->Value();
      Element* element = ElementTraversal::FirstWithin(root_node);
      while (element) {
        QUERY_STATS_INCREMENT(fast_class);
        if (HasClassName(*element, class_name)) {
          ExecuteForTraverseRoot<SelectorQueryTrait>(*element, root_node,
                                                     output);
          element =
              ElementTraversal::NextSkippingChildren(*element, &root_node);
        } else {
          element = ElementTraversal::Next(*element, &root_node);
        }
      }
      return;
    }

    if (selector->Relation() == CSSSelector::kSubSelector)
      continue;
    is_rightmost_selector = false;
    if (selector->Relation() == CSSSelector::kDirectAdjacent ||
        selector->Relation() == CSSSelector::kIndirectAdjacent)
      start_from_parent = true;
    else
      start_from_parent = false;
  }

  ExecuteForTraverseRoot<SelectorQueryTrait>(root_node, root_node, output);
}

template <typename SelectorQueryTrait>
void SelectorQuery::ExecuteForTraverseRoot(
    ContainerNode& traverse_root,
    ContainerNode& root_node,
    typename SelectorQueryTrait::OutputType& output) const {
  DCHECK_EQ(selectors_.size(), 1u);

  const CSSSelector& selector = *selectors_[0];

  for (Element& element : ElementTraversal::DescendantsOf(traverse_root)) {
    QUERY_STATS_INCREMENT(fast_scan);
    if (SelectorMatches(selector, element, root_node)) {
      SelectorQueryTrait::AppendElement(output, element);
      if (SelectorQueryTrait::kShouldOnlyMatchFirstElement)
        return;
    }
  }
}

bool SelectorQuery::SelectorListMatches(ContainerNode& root_node,
                                        Element& element) const {
  for (const auto& selector : selectors_) {
    if (SelectorMatches(*selector, element, root_node))
      return true;
  }
  return false;
}

template <typename SelectorQueryTrait>
void SelectorQuery::ExecuteSlow(
    ContainerNode& root_node,
    typename SelectorQueryTrait::OutputType& output) const {
  for (Element& element : ElementTraversal::DescendantsOf(root_node)) {
    QUERY_STATS_INCREMENT(slow_scan);
    if (!SelectorListMatches(root_node, element))
      continue;
    SelectorQueryTrait::AppendElement(output, element);
    if (SelectorQueryTrait::kShouldOnlyMatchFirstElement)
      return;
  }
}

// FIXME: Move the following helper functions, authorShadowRootOf,
// firstWithinTraversingShadowTree, nextTraversingShadowTree to the best place,
// e.g. NodeTraversal.
static ShadowRoot* AuthorShadowRootOf(const ContainerNode& node) {
  if (!node.IsElementNode())
    return nullptr;
  ElementShadow* shadow = ToElement(node).Shadow();
  if (!shadow)
    return nullptr;

  for (ShadowRoot* shadow_root = &shadow->OldestShadowRoot(); shadow_root;
       shadow_root = shadow_root->YoungerShadowRoot()) {
    if (shadow_root->IsOpenOrV0())
      return shadow_root;
  }
  return nullptr;
}

static ContainerNode* NextTraversingShadowTree(const ContainerNode& node,
                                               const ContainerNode* root_node) {
  if (ShadowRoot* shadow_root = AuthorShadowRootOf(node))
    return shadow_root;

  const ContainerNode* current = &node;
  while (current) {
    if (Element* next = ElementTraversal::Next(*current, root_node))
      return next;

    if (!current->IsInShadowTree())
      return nullptr;

    ShadowRoot* shadow_root = current->ContainingShadowRoot();
    if (shadow_root == root_node)
      return nullptr;
    if (ShadowRoot* younger_shadow_root = shadow_root->YoungerShadowRoot()) {
      DCHECK(younger_shadow_root->IsOpenOrV0());
      return younger_shadow_root;
    }

    current = &shadow_root->host();
  }
  return nullptr;
}

template <typename SelectorQueryTrait>
void SelectorQuery::ExecuteSlowTraversingShadowTree(
    ContainerNode& root_node,
    typename SelectorQueryTrait::OutputType& output) const {
  for (ContainerNode* node = NextTraversingShadowTree(root_node, &root_node);
       node; node = NextTraversingShadowTree(*node, &root_node)) {
    if (!node->IsElementNode())
      continue;
    QUERY_STATS_INCREMENT(slow_traversing_shadow_tree_scan);
    Element* element = ToElement(node);
    if (!SelectorListMatches(root_node, *element))
      continue;
    SelectorQueryTrait::AppendElement(output, *element);
    if (SelectorQueryTrait::kShouldOnlyMatchFirstElement)
      return;
  }
}

static const CSSSelector* SelectorForIdLookup(
    const CSSSelector& first_selector) {
  for (const CSSSelector* selector = &first_selector; selector;
       selector = selector->TagHistory()) {
    if (selector->Match() == CSSSelector::kId)
      return selector;
    // We only use the fast path when in standards mode where #id selectors
    // are case sensitive, so we need the same behavior for [id=value].
    if (selector->Match() == CSSSelector::kAttributeExact &&
        selector->Attribute() == idAttr &&
        selector->AttributeMatch() == CSSSelector::kCaseSensitive)
      return selector;
    if (selector->Relation() != CSSSelector::kSubSelector)
      break;
  }
  return nullptr;
}

template <typename SelectorQueryTrait>
void SelectorQuery::Execute(
    ContainerNode& root_node,
    typename SelectorQueryTrait::OutputType& output) const {
  if (selectors_.IsEmpty())
    return;

  if (!CanUseFastQuery(root_node)) {
    if (needs_updated_distribution_)
      root_node.UpdateDistribution();
    if (uses_deep_combinator_or_shadow_pseudo_) {
      ExecuteSlowTraversingShadowTree<SelectorQueryTrait>(root_node, output);
    } else {
      ExecuteSlow<SelectorQueryTrait>(root_node, output);
    }
    return;
  }

  DCHECK_EQ(selectors_.size(), 1u);
  DCHECK(!root_node.GetDocument().InQuirksMode());

  const CSSSelector& selector = *selectors_[0];
  const CSSSelector& first_selector = selector;

  // Fast path for querySelector*('#id'), querySelector*('tag#id'),
  // querySelector*('tag[id=example]').
  if (const CSSSelector* id_selector = SelectorForIdLookup(first_selector)) {
    const AtomicString& id_to_match = id_selector->Value();
    if (root_node.GetTreeScope().ContainsMultipleElementsWithId(id_to_match)) {
      const HeapVector<Member<Element>>& elements =
          root_node.GetTreeScope().GetAllElementsById(id_to_match);
      for (const auto& element : elements) {
        if (!element->IsDescendantOf(&root_node))
          continue;
        QUERY_STATS_INCREMENT(fast_id);
        if (SelectorMatches(selector, *element, root_node)) {
          SelectorQueryTrait::AppendElement(output, *element);
          if (SelectorQueryTrait::kShouldOnlyMatchFirstElement)
            return;
        }
      }
      return;
    }
    Element* element = root_node.GetTreeScope().GetElementById(id_to_match);
    if (!element)
      return;
    if (!element->IsDescendantOf(&root_node))
      return;
    QUERY_STATS_INCREMENT(fast_id);
    if (SelectorMatches(selector, *element, root_node))
      SelectorQueryTrait::AppendElement(output, *element);
    return;
  }

  if (!first_selector.TagHistory()) {
    // Fast path for querySelector*('.foo'), and querySelector*('div').
    switch (first_selector.Match()) {
      case CSSSelector::kClass:
        CollectElementsByClassName<SelectorQueryTrait>(
            root_node, first_selector.Value(), nullptr, output);
        return;
      case CSSSelector::kTag:
        if (first_selector.TagQName().NamespaceURI() == g_star_atom) {
          CollectElementsByTagName<SelectorQueryTrait>(
              root_node, first_selector.TagQName(), output);
          return;
        }
        // querySelector*() doesn't allow namespace prefix resolution and
        // throws before we get here, but we still may have selectors for
        // elements without a namespace.
        DCHECK_EQ(first_selector.TagQName().NamespaceURI(), g_null_atom);
        break;
      default:
        break;  // If we need another fast path, add here.
    }
  }

  FindTraverseRootsAndExecute<SelectorQueryTrait>(root_node, output);
}

std::unique_ptr<SelectorQuery> SelectorQuery::Adopt(
    CSSSelectorList selector_list) {
  return WTF::WrapUnique(new SelectorQuery(std::move(selector_list)));
}

SelectorQuery::SelectorQuery(CSSSelectorList selector_list)
    : selector_list_(std::move(selector_list)),
      uses_deep_combinator_or_shadow_pseudo_(false),
      needs_updated_distribution_(false) {
  selectors_.ReserveInitialCapacity(selector_list_.ComputeLength());
  for (const CSSSelector* selector = selector_list_.First(); selector;
       selector = CSSSelectorList::Next(*selector)) {
    if (selector->MatchesPseudoElement())
      continue;
    selectors_.UncheckedAppend(selector);
    uses_deep_combinator_or_shadow_pseudo_ |=
        selector->HasDeepCombinatorOrShadowPseudo();
    needs_updated_distribution_ |= selector->NeedsUpdatedDistribution();
  }
}

SelectorQuery* SelectorQueryCache::Add(const AtomicString& selectors,
                                       const Document& document,
                                       ExceptionState& exception_state) {
  if (selectors.IsEmpty()) {
    exception_state.ThrowDOMException(kSyntaxError,
                                      "The provided selector is empty.");
    return nullptr;
  }

  HashMap<AtomicString, std::unique_ptr<SelectorQuery>>::iterator it =
      entries_.Find(selectors);
  if (it != entries_.end())
    return it->value.get();

  CSSSelectorList selector_list = CSSParser::ParseSelector(
      CSSParserContext::Create(document, document.BaseURL(),
                               document.GetReferrerPolicy(), g_empty_string,
                               CSSParserContext::kStaticProfile),
      nullptr, selectors);

  if (!selector_list.First()) {
    exception_state.ThrowDOMException(
        kSyntaxError, "'" + selectors + "' is not a valid selector.");
    return nullptr;
  }

  const unsigned kMaximumSelectorQueryCacheSize = 256;
  if (entries_.size() == kMaximumSelectorQueryCacheSize)
    entries_.erase(entries_.begin());

  return entries_
      .insert(selectors, SelectorQuery::Adopt(std::move(selector_list)))
      .stored_value->value.get();
}

void SelectorQueryCache::Invalidate() {
  entries_.Clear();
}

}  // namespace blink
