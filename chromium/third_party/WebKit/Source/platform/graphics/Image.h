/*
 * Copyright (C) 2006 Samuel Weinig (sam.weinig@gmail.com)
 * Copyright (C) 2004, 2005, 2006 Apple Computer, Inc.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE COMPUTER, INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE COMPUTER, INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef Image_h
#define Image_h

#include "platform/PlatformExport.h"
#include "platform/SharedBuffer.h"
#include "platform/geometry/IntRect.h"
#include "platform/graphics/Color.h"
#include "platform/graphics/GraphicsTypes.h"
#include "platform/graphics/ImageAnimationPolicy.h"
#include "platform/graphics/ImageObserver.h"
#include "platform/graphics/ImageOrientation.h"
#include "platform/graphics/paint/PaintCanvas.h"
#include "platform/graphics/paint/PaintFlags.h"
#include "platform/wtf/Assertions.h"
#include "platform/wtf/Noncopyable.h"
#include "platform/wtf/PassRefPtr.h"
#include "platform/wtf/RefPtr.h"
#include "platform/wtf/ThreadSafeRefCounted.h"
#include "platform/wtf/text/WTFString.h"
#include "third_party/skia/include/core/SkRefCnt.h"

class SkImage;
class SkMatrix;

namespace blink {

class FloatPoint;
class FloatRect;
class FloatSize;
class GraphicsContext;
class Image;

class PLATFORM_EXPORT Image : public ThreadSafeRefCounted<Image> {
  friend class GeneratedImage;
  friend class CrossfadeGeneratedImage;
  friend class GradientGeneratedImage;
  friend class GraphicsContext;
  WTF_MAKE_NONCOPYABLE(Image);

 public:
  virtual ~Image();

  static PassRefPtr<Image> LoadPlatformResource(const char* name);
  static bool SupportsType(const String&);

  virtual bool IsSVGImage() const { return false; }
  virtual bool IsBitmapImage() const { return false; }

  // To increase accuracy of currentFrameKnownToBeOpaque() it may,
  // for applicable image types, be told to pre-cache metadata for
  // the current frame. Since this may initiate a deferred image
  // decoding, PreCacheMetadata requires a InspectorPaintImageEvent
  // during call.
  enum MetadataMode { kUseCurrentMetadata, kPreCacheMetadata };
  virtual bool CurrentFrameKnownToBeOpaque(
      MetadataMode = kUseCurrentMetadata) = 0;

  virtual bool CurrentFrameIsComplete() { return false; }
  virtual bool CurrentFrameIsLazyDecoded() { return false; }
  virtual bool IsTextureBacked() const { return false; }

  // Derived classes should override this if they can assure that the current
  // image frame contains only resources from its own security origin.
  virtual bool CurrentFrameHasSingleSecurityOrigin() const { return false; }

  static Image* NullImage();
  bool IsNull() const { return Size().IsEmpty(); }

  virtual bool UsesContainerSize() const { return false; }
  virtual bool HasRelativeSize() const { return false; }

  virtual IntSize Size() const = 0;
  IntRect Rect() const { return IntRect(IntPoint(), Size()); }
  int width() const { return Size().Width(); }
  int height() const { return Size().Height(); }
  virtual bool GetHotSpot(IntPoint&) const { return false; }

  enum SizeAvailability { kSizeAvailable, kSizeUnavailable };
  virtual SizeAvailability SetData(PassRefPtr<SharedBuffer> data,
                                   bool all_data_received);
  virtual SizeAvailability DataChanged(bool /*allDataReceived*/) {
    return kSizeUnavailable;
  }

  virtual String FilenameExtension() const {
    return String();
  }  // null string if unknown

  virtual void DestroyDecodedData() = 0;

  virtual PassRefPtr<SharedBuffer> Data() { return encoded_image_data_; }

  // Animation begins whenever someone draws the image, so startAnimation() is
  // not normally called. It will automatically pause once all observers no
  // longer want to render the image anywhere.
  enum CatchUpAnimation { kDoNotCatchUp, kCatchUp };
  virtual void StartAnimation(CatchUpAnimation = kCatchUp) {}
  virtual void ResetAnimation() {}

  // True if this image can potentially animate.
  virtual bool MaybeAnimated() { return false; }

  // Set animationPolicy
  virtual void SetAnimationPolicy(ImageAnimationPolicy) {}
  virtual ImageAnimationPolicy AnimationPolicy() {
    return kImageAnimationPolicyAllowed;
  }
  virtual void AdvanceTime(double delta_time_in_seconds) {}

  // Advances an animated image. For BitmapImage (e.g., animated gifs) this
  // will advance to the next frame. For SVGImage, this will trigger an
  // animation update for CSS and advance the SMIL timeline by one frame.
  virtual void AdvanceAnimationForTesting() {}

  // Typically the ImageResourceContent that owns us.
  ImageObserver* GetImageObserver() const {
    return image_observer_disabled_ ? nullptr : image_observer_;
  }
  void ClearImageObserver() { image_observer_ = nullptr; }
  // To avoid interleaved accesses to |m_imageObserverDisabled|, do not call
  // setImageObserverDisabled() other than from ImageObserverDisabler.
  void SetImageObserverDisabled(bool disabled) {
    image_observer_disabled_ = disabled;
  }

  enum TileRule { kStretchTile, kRoundTile, kSpaceTile, kRepeatTile };

  virtual sk_sp<SkImage> ImageForCurrentFrame() = 0;
  virtual PassRefPtr<Image> ImageForDefaultFrame();

  enum ImageClampingMode {
    kClampImageToSourceRect,
    kDoNotClampImageToSourceRect
  };

  virtual void Draw(PaintCanvas*,
                    const PaintFlags&,
                    const FloatRect& dst_rect,
                    const FloatRect& src_rect,
                    RespectImageOrientationEnum,
                    ImageClampingMode) = 0;

  virtual bool ApplyShader(PaintFlags&, const SkMatrix& local_matrix);

  // Compute the tile which contains a given point (assuming a repeating tile
  // grid). The point and returned value are in destination grid space.
  static FloatRect ComputeTileContaining(const FloatPoint&,
                                         const FloatSize& tile_size,
                                         const FloatPoint& tile_phase,
                                         const FloatSize& tile_spacing);

  // Compute the image subset which gets mapped onto |dest|, when the whole
  // image is drawn into |tile|.  Assumes |tile| contains |dest|.  The tile rect
  // is in destination grid space while the return value is in image coordinate
  // space.
  static FloatRect ComputeSubsetForTile(const FloatRect& tile,
                                        const FloatRect& dest,
                                        const FloatSize& image_size);

 protected:
  Image(ImageObserver* = 0);

  void DrawTiledBackground(GraphicsContext&,
                           const FloatRect& dst_rect,
                           const FloatPoint& src_point,
                           const FloatSize& tile_size,
                           SkBlendMode,
                           const FloatSize& repeat_spacing);
  void DrawTiledBorder(GraphicsContext&,
                       const FloatRect& dst_rect,
                       const FloatRect& src_rect,
                       const FloatSize& tile_scale_factor,
                       TileRule h_rule,
                       TileRule v_rule,
                       SkBlendMode);

  virtual void DrawPattern(GraphicsContext&,
                           const FloatRect&,
                           const FloatSize&,
                           const FloatPoint& phase,
                           SkBlendMode,
                           const FloatRect&,
                           const FloatSize& repeat_spacing = FloatSize());

 private:
  RefPtr<SharedBuffer> encoded_image_data_;
  // TODO(Oilpan): consider having Image on the Oilpan heap and
  // turn this into a Member<>.
  //
  // The observer (an ImageResourceContent) is an untraced member, with the
  // ImageResourceContent being responsible for clearing itself out.
  UntracedMember<ImageObserver> image_observer_;
  bool image_observer_disabled_;
};

#define DEFINE_IMAGE_TYPE_CASTS(typeName)                          \
  DEFINE_TYPE_CASTS(typeName, Image, image, image->Is##typeName(), \
                    image.Is##typeName())

}  // namespace blink

#endif
