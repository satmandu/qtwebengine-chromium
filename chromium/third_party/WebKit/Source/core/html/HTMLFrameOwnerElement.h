/*
 * Copyright (C) 2006, 2007, 2009 Apple Inc. All rights reserved.
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

#ifndef HTMLFrameOwnerElement_h
#define HTMLFrameOwnerElement_h

#include "core/CoreExport.h"
#include "core/dom/Document.h"
#include "core/frame/FrameOwner.h"
#include "core/html/HTMLElement.h"
#include "platform/heap/Handle.h"
#include "platform/scroll/ScrollTypes.h"
#include "platform/weborigin/SecurityPolicy.h"
#include "platform/wtf/HashCountedSet.h"

namespace blink {

class ExceptionState;
class Frame;
class FrameViewBase;
class LayoutPart;

class CORE_EXPORT HTMLFrameOwnerElement : public HTMLElement,
                                          public FrameOwner {
  USING_GARBAGE_COLLECTED_MIXIN(HTMLFrameOwnerElement);

 public:
  ~HTMLFrameOwnerElement() override;

  DOMWindow* contentWindow() const;
  Document* contentDocument() const;

  virtual void DisconnectContentFrame();

  // Most subclasses use LayoutPart (either LayoutEmbeddedObject or
  // LayoutIFrame) except for HTMLObjectElement and HTMLEmbedElement which may
  // return any LayoutObject when using fallback content.
  LayoutPart* GetLayoutPart() const;

  Document* getSVGDocument(ExceptionState&) const;

  virtual bool LoadedNonEmptyDocument() const { return false; }
  virtual void DidLoadNonEmptyDocument() {}

  void SetWidget(FrameViewBase*);
  FrameViewBase* ReleaseWidget();
  FrameViewBase* OwnedWidget() const;

  class UpdateSuspendScope {
    STACK_ALLOCATED();

   public:
    UpdateSuspendScope();
    ~UpdateSuspendScope();

   private:
    void PerformDeferredWidgetTreeOperations();
  };

  // FrameOwner overrides:
  Frame* ContentFrame() const final { return content_frame_; }
  void SetContentFrame(Frame&) final;
  void ClearContentFrame() final;
  void DispatchLoad() final;
  SandboxFlags GetSandboxFlags() const final { return sandbox_flags_; }
  bool CanRenderFallbackContent() const override { return false; }
  void RenderFallbackContent() override {}
  AtomicString BrowsingContextContainerName() const override {
    return getAttribute(HTMLNames::nameAttr);
  }
  ScrollbarMode ScrollingMode() const override { return kScrollbarAuto; }
  int MarginWidth() const override { return -1; }
  int MarginHeight() const override { return -1; }
  bool AllowFullscreen() const override { return false; }
  bool AllowPaymentRequest() const override { return false; }
  bool IsDisplayNone() const override { return !widget_; }
  AtomicString Csp() const override { return g_null_atom; }
  const WebVector<WebFeaturePolicyFeature>& AllowedFeatures() const override;

  DECLARE_VIRTUAL_TRACE();

 protected:
  HTMLFrameOwnerElement(const QualifiedName& tag_name, Document&);
  void SetSandboxFlags(SandboxFlags);

  bool LoadOrRedirectSubframe(const KURL&,
                              const AtomicString& frame_name,
                              bool replace_current_item);
  bool IsKeyboardFocusable() const override;

  void DisposeWidgetSoon(FrameViewBase*);
  void FrameOwnerPropertiesChanged();

 private:
  // Intentionally private to prevent redundant checks when the type is
  // already HTMLFrameOwnerElement.
  bool IsLocal() const final { return true; }
  bool IsRemote() const final { return false; }

  bool IsFrameOwnerElement() const final { return true; }

  virtual ReferrerPolicy ReferrerPolicyAttribute() {
    return kReferrerPolicyDefault;
  }

  Member<Frame> content_frame_;
  Member<FrameViewBase> widget_;
  SandboxFlags sandbox_flags_;
};

DEFINE_ELEMENT_TYPE_CASTS(HTMLFrameOwnerElement, IsFrameOwnerElement());

class SubframeLoadingDisabler {
  STACK_ALLOCATED();

 public:
  explicit SubframeLoadingDisabler(Node& root)
      : SubframeLoadingDisabler(&root) {}

  explicit SubframeLoadingDisabler(Node* root) : root_(root) {
    if (root_)
      DisabledSubtreeRoots().insert(root_);
  }

  ~SubframeLoadingDisabler() {
    if (root_)
      DisabledSubtreeRoots().erase(root_);
  }

  static bool CanLoadFrame(HTMLFrameOwnerElement& owner) {
    for (Node* node = &owner; node; node = node->ParentOrShadowHostNode()) {
      if (DisabledSubtreeRoots().Contains(node))
        return false;
    }
    return true;
  }

 private:
  // The use of UntracedMember<Node>  is safe as all SubtreeRootSet
  // references are on the stack and reachable in case a conservative
  // GC hits.
  // TODO(sof): go back to HeapHashSet<> once crbug.com/684551 has been
  // resolved.
  using SubtreeRootSet = HashCountedSet<UntracedMember<Node>>;

  CORE_EXPORT static SubtreeRootSet& DisabledSubtreeRoots();

  Member<Node> root_;
};

DEFINE_TYPE_CASTS(HTMLFrameOwnerElement,
                  FrameOwner,
                  owner,
                  owner->IsLocal(),
                  owner.IsLocal());

}  // namespace blink

#endif  // HTMLFrameOwnerElement_h
