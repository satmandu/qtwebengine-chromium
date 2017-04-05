// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "extensions/browser/image_loader.h"

#include <stddef.h>

#include <map>
#include <vector>

#include "base/callback.h"
#include "base/compiler_specific.h"
#include "base/files/file_util.h"
#include "base/strings/string_number_conversions.h"
#include "base/task_scheduler/post_task.h"
#include "base/threading/sequenced_worker_pool.h"
#include "content/public/browser/browser_thread.h"
#include "extensions/browser/component_extension_resource_manager.h"
#include "extensions/browser/extensions_browser_client.h"
#include "extensions/browser/image_loader_factory.h"
#include "extensions/common/extension.h"
#include "extensions/common/manifest_handlers/icons_handler.h"
#include "skia/ext/image_operations.h"
#include "ui/base/layout.h"
#include "ui/base/resource/resource_bundle.h"
#include "ui/display/display.h"
#include "ui/display/screen.h"
#include "ui/gfx/codec/png_codec.h"
#include "ui/gfx/image/image_family.h"
#include "ui/gfx/image/image_skia.h"

using content::BrowserThread;

namespace extensions {

namespace {

bool ShouldResizeImageRepresentation(
    ImageLoader::ImageRepresentation::ResizeCondition resize_method,
    const gfx::Size& decoded_size,
    const gfx::Size& desired_size) {
  switch (resize_method) {
    case ImageLoader::ImageRepresentation::ALWAYS_RESIZE:
      return decoded_size != desired_size;
    case ImageLoader::ImageRepresentation::RESIZE_WHEN_LARGER:
      return decoded_size.width() > desired_size.width() ||
             decoded_size.height() > desired_size.height();
    case ImageLoader::ImageRepresentation::NEVER_RESIZE:
      return false;
    default:
      NOTREACHED();
      return false;
  }
}

SkBitmap ResizeIfNeeded(const SkBitmap& bitmap,
                        const ImageLoader::ImageRepresentation& image_info) {
  gfx::Size original_size(bitmap.width(), bitmap.height());
  if (ShouldResizeImageRepresentation(image_info.resize_condition,
                                      original_size,
                                      image_info.desired_size)) {
    return skia::ImageOperations::Resize(
        bitmap, skia::ImageOperations::RESIZE_LANCZOS3,
        image_info.desired_size.width(), image_info.desired_size.height());
  }

  return bitmap;
}

void LoadResourceOnUIThread(int resource_id, SkBitmap* bitmap) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  gfx::ImageSkia image(
      *ResourceBundle::GetSharedInstance().GetImageSkiaNamed(resource_id));
  image.MakeThreadSafe();
  *bitmap = *image.bitmap();
}

void LoadImageBlocking(const ImageLoader::ImageRepresentation& image_info,
                       SkBitmap* bitmap) {
  // Read the file from disk.
  std::string file_contents;
  base::FilePath path = image_info.resource.GetFilePath();
  if (path.empty() || !base::ReadFileToString(path, &file_contents)) {
    return;
  }

  const unsigned char* data =
      reinterpret_cast<const unsigned char*>(file_contents.data());
  // Note: This class only decodes bitmaps from extension resources. Chrome
  // doesn't (for security reasons) directly load extension resources provided
  // by the extension author, but instead decodes them in a separate
  // locked-down utility process. Only if the decoding succeeds is the image
  // saved from memory to disk and subsequently used in the Chrome UI.
  // Chrome is therefore decoding bitmaps here that were generated by Chrome.
  gfx::PNGCodec::Decode(data, file_contents.length(), bitmap);
}

std::vector<SkBitmap> LoadResourceBitmaps(
    const Extension* extension,
    const std::vector<ImageLoader::ImageRepresentation>& info_list) {
  // Loading resources has to happen on the UI thread. So do this first, and
  // pass the rest of the work off as a blocking pool task.
  std::vector<SkBitmap> bitmaps;
  bitmaps.resize(info_list.size());

  int i = 0;
  for (std::vector<ImageLoader::ImageRepresentation>::const_iterator
           it = info_list.begin();
       it != info_list.end();
       ++it, ++i) {
    DCHECK(it->resource.relative_path().empty() ||
           extension->path() == it->resource.extension_root());

    int resource_id;
    if (extension->location() == Manifest::COMPONENT) {
      const extensions::ComponentExtensionResourceManager* manager =
          extensions::ExtensionsBrowserClient::Get()
              ->GetComponentExtensionResourceManager();
      if (manager && manager->IsComponentExtensionResource(
              extension->path(), it->resource.relative_path(), &resource_id)) {
        LoadResourceOnUIThread(resource_id, &bitmaps[i]);
      }
    }
  }
  return bitmaps;
}

}  // namespace

////////////////////////////////////////////////////////////////////////////////
// ImageLoader::ImageRepresentation

ImageLoader::ImageRepresentation::ImageRepresentation(
    const ExtensionResource& resource,
    ResizeCondition resize_condition,
    const gfx::Size& desired_size,
    float scale_factor)
    : resource(resource),
      resize_condition(resize_condition),
      desired_size(desired_size),
      scale_factor(scale_factor) {}

ImageLoader::ImageRepresentation::~ImageRepresentation() {
}

////////////////////////////////////////////////////////////////////////////////
// ImageLoader::LoadResult

struct ImageLoader::LoadResult  {
  LoadResult(const SkBitmap& bitmap,
             const gfx::Size& original_size,
             const ImageRepresentation& image_representation);
  ~LoadResult();

  SkBitmap bitmap;
  gfx::Size original_size;
  ImageRepresentation image_representation;
};

ImageLoader::LoadResult::LoadResult(
    const SkBitmap& bitmap,
    const gfx::Size& original_size,
    const ImageLoader::ImageRepresentation& image_representation)
    : bitmap(bitmap),
      original_size(original_size),
      image_representation(image_representation) {
}

ImageLoader::LoadResult::~LoadResult() {
}

namespace {

// Need to be after ImageRepresentation and LoadResult are defined.
std::vector<ImageLoader::LoadResult> LoadImagesBlocking(
    const std::vector<ImageLoader::ImageRepresentation>& info_list,
    const std::vector<SkBitmap>& bitmaps) {
  std::vector<ImageLoader::LoadResult> load_result;

  for (size_t i = 0; i < info_list.size(); ++i) {
    const ImageLoader::ImageRepresentation& image = info_list[i];

    // If we don't have a path there isn't anything we can do, just skip it.
    if (image.resource.relative_path().empty())
      continue;

    SkBitmap bitmap;
    if (bitmaps[i].isNull())
      LoadImageBlocking(image, &bitmap);
    else
      bitmap = bitmaps[i];

    // If the image failed to load, skip it.
    if (bitmap.isNull() || bitmap.empty())
      continue;

    gfx::Size original_size(bitmap.width(), bitmap.height());
    bitmap = ResizeIfNeeded(bitmap, image);

    load_result.push_back(
        ImageLoader::LoadResult(bitmap, original_size, image));
  }

  return load_result;
}

}  // namespace

////////////////////////////////////////////////////////////////////////////////
// ImageLoader

ImageLoader::ImageLoader()
    : weak_ptr_factory_(this) {
}

ImageLoader::~ImageLoader() {
}

// static
ImageLoader* ImageLoader::Get(content::BrowserContext* context) {
  return ImageLoaderFactory::GetForBrowserContext(context);
}

void ImageLoader::LoadImageAsync(const Extension* extension,
                                 const ExtensionResource& resource,
                                 const gfx::Size& max_size,
                                 const ImageLoaderImageCallback& callback) {
  std::vector<ImageRepresentation> info_list;
  info_list.push_back(ImageRepresentation(
      resource, ImageRepresentation::RESIZE_WHEN_LARGER, max_size, 1.f));
  LoadImagesAsync(extension, info_list, callback);
}

void ImageLoader::LoadImageAtEveryScaleFactorAsync(
    const Extension* extension,
    const gfx::Size& dip_size,
    const ImageLoaderImageCallback& callback) {
  std::vector<ImageRepresentation> info_list;

  std::set<float> scales;
  for (auto scale : ui::GetSupportedScaleFactors())
    scales.insert(ui::GetScaleForScaleFactor(scale));

  // There may not be a screen in unit tests.
  auto* screen = display::Screen::GetScreen();
  if (screen) {
    for (const auto& display : screen->GetAllDisplays())
      scales.insert(display.device_scale_factor());
  }

  for (auto scale : scales) {
    const gfx::Size px_size = gfx::ScaleToFlooredSize(dip_size, scale);
    ExtensionResource image = IconsInfo::GetIconResource(
        extension, px_size.width(), ExtensionIconSet::MATCH_BIGGER);
    info_list.push_back(ImageRepresentation(
        image, ImageRepresentation::ALWAYS_RESIZE, px_size, scale));
  }
  LoadImagesAsync(extension, info_list, callback);
}

void ImageLoader::LoadImagesAsync(
    const Extension* extension,
    const std::vector<ImageRepresentation>& info_list,
    const ImageLoaderImageCallback& callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  DCHECK(!BrowserThread::GetBlockingPool()->RunsTasksOnCurrentThread());
  base::PostTaskWithTraitsAndReplyWithResult(
      FROM_HERE, base::TaskTraits().MayBlock().WithPriority(
                     base::TaskPriority::USER_VISIBLE),
      base::Bind(LoadImagesBlocking, info_list,
                 LoadResourceBitmaps(extension, info_list)),
      base::Bind(&ImageLoader::ReplyBack, weak_ptr_factory_.GetWeakPtr(),
                 callback));
}

void ImageLoader::LoadImageFamilyAsync(
    const Extension* extension,
    const std::vector<ImageRepresentation>& info_list,
    const ImageLoaderImageFamilyCallback& callback) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);
  DCHECK(!BrowserThread::GetBlockingPool()->RunsTasksOnCurrentThread());
  base::PostTaskWithTraitsAndReplyWithResult(
      FROM_HERE, base::TaskTraits().MayBlock().WithPriority(
                     base::TaskPriority::USER_VISIBLE),
      base::Bind(LoadImagesBlocking, info_list,
                 LoadResourceBitmaps(extension, info_list)),
      base::Bind(&ImageLoader::ReplyBackWithImageFamily,
                 weak_ptr_factory_.GetWeakPtr(), callback));
}

void ImageLoader::ReplyBack(const ImageLoaderImageCallback& callback,
                            const std::vector<LoadResult>& load_result) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  gfx::ImageSkia image_skia;

  for (std::vector<LoadResult>::const_iterator it = load_result.begin();
       it != load_result.end(); ++it) {
    const SkBitmap& bitmap = it->bitmap;
    const ImageRepresentation& image_rep = it->image_representation;

    image_skia.AddRepresentation(
        gfx::ImageSkiaRep(bitmap, image_rep.scale_factor));
  }

  gfx::Image image;
  if (!image_skia.isNull()) {
    image_skia.MakeThreadSafe();
    image = gfx::Image(image_skia);
  }

  callback.Run(image);
}

void ImageLoader::ReplyBackWithImageFamily(
    const ImageLoaderImageFamilyCallback& callback,
    const std::vector<LoadResult>& load_result) {
  DCHECK_CURRENTLY_ON(BrowserThread::UI);

  std::map<std::pair<int, int>, gfx::ImageSkia> image_skia_map;
  gfx::ImageFamily image_family;

  for (std::vector<LoadResult>::const_iterator it = load_result.begin();
       it != load_result.end();
       ++it) {
    const SkBitmap& bitmap = it->bitmap;
    const ImageRepresentation& image_rep = it->image_representation;
    const std::pair<int, int> key = std::make_pair(
        image_rep.desired_size.width(), image_rep.desired_size.height());
    // Create a new ImageSkia for this width/height, or add a representation to
    // an existing ImageSkia with the same width/height.
    image_skia_map[key].AddRepresentation(
        gfx::ImageSkiaRep(bitmap, image_rep.scale_factor));
  }

  for (std::map<std::pair<int, int>, gfx::ImageSkia>::iterator it =
           image_skia_map.begin();
       it != image_skia_map.end();
       ++it) {
    it->second.MakeThreadSafe();
    image_family.Add(it->second);
  }

  callback.Run(image_family);
}

}  // namespace extensions
