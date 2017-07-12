// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/tiles/gpu_image_decode_cache.h"

#include "cc/paint/draw_image.h"
#include "cc/test/test_context_provider.h"
#include "cc/test/test_tile_task_runner.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/skia/include/core/SkRefCnt.h"

namespace cc {
namespace {

gfx::ColorSpace DefaultColorSpace() {
  return gfx::ColorSpace::CreateSRGB();
}

size_t kGpuMemoryLimitBytes = 96 * 1024 * 1024;
class TestGpuImageDecodeCache : public GpuImageDecodeCache {
 public:
  explicit TestGpuImageDecodeCache(ContextProvider* context)
      : GpuImageDecodeCache(context,
                            ResourceFormat::RGBA_8888,
                            kGpuMemoryLimitBytes,
                            kGpuMemoryLimitBytes) {}
};

sk_sp<SkImage> CreateImage(int width, int height) {
  SkBitmap bitmap;
  gfx::ColorSpace color_space = gfx::ColorSpace::CreateSRGB();
  bitmap.allocPixels(
      SkImageInfo::MakeN32Premul(width, height, color_space.ToSkColorSpace()));
  return SkImage::MakeFromBitmap(bitmap);
}

SkMatrix CreateMatrix(const SkSize& scale, bool is_decomposable) {
  SkMatrix matrix;
  matrix.setScale(scale.width(), scale.height());

  if (!is_decomposable) {
    // Perspective is not decomposable, add it.
    matrix[SkMatrix::kMPersp0] = 0.1f;
  }

  return matrix;
}

TEST(GpuImageDecodeCacheTest, GetTaskForImageSameImage) {
  auto context_provider = TestContextProvider::Create();
  context_provider->BindToCurrentThread();
  TestGpuImageDecodeCache cache(context_provider.get());
  sk_sp<SkImage> image = CreateImage(100, 100);
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  DrawImage draw_image(image, SkIRect::MakeWH(image->width(), image->height()),
                       quality,
                       CreateMatrix(SkSize::Make(1.5f, 1.5f), is_decomposable),
                       DefaultColorSpace());
  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(task);

  DrawImage another_draw_image(
      image, SkIRect::MakeWH(image->width(), image->height()), quality,
      CreateMatrix(SkSize::Make(1.5f, 1.5f), is_decomposable),
      DefaultColorSpace());
  scoped_refptr<TileTask> another_task;
  need_unref = cache.GetTaskForImageAndRef(
      another_draw_image, ImageDecodeCache::TracingInfo(), &another_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(task.get() == another_task.get());

  TestTileTaskRunner::ProcessTask(task->dependencies()[0].get());
  TestTileTaskRunner::ProcessTask(task.get());

  cache.UnrefImage(draw_image);
  cache.UnrefImage(draw_image);
}

TEST(GpuImageDecodeCacheTest, GetTaskForImageSmallerScale) {
  auto context_provider = TestContextProvider::Create();
  context_provider->BindToCurrentThread();
  TestGpuImageDecodeCache cache(context_provider.get());
  sk_sp<SkImage> image = CreateImage(100, 100);
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  DrawImage draw_image(image, SkIRect::MakeWH(image->width(), image->height()),
                       quality,
                       CreateMatrix(SkSize::Make(1.5f, 1.5f), is_decomposable),
                       DefaultColorSpace());
  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(task);

  DrawImage another_draw_image(
      image, SkIRect::MakeWH(image->width(), image->height()), quality,
      CreateMatrix(SkSize::Make(0.5f, 0.5f), is_decomposable),
      DefaultColorSpace());
  scoped_refptr<TileTask> another_task;
  need_unref = cache.GetTaskForImageAndRef(
      another_draw_image, ImageDecodeCache::TracingInfo(), &another_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(task.get() == another_task.get());

  TestTileTaskRunner::ProcessTask(task->dependencies()[0].get());
  TestTileTaskRunner::ProcessTask(task.get());

  cache.UnrefImage(draw_image);
  cache.UnrefImage(another_draw_image);
}

// crbug.com/709341.
#if defined(MEMORY_SANITIZER)
#define MAYBE_GetTaskForImageLowerQuality DISABLED_GetTaskForImageLowerQuality
#else
#define MAYBE_GetTaskForImageLowerQuality GetTaskForImageLowerQuality
#endif
TEST(GpuImageDecodeCacheTest, MAYBE_GetTaskForImageLowerQuality) {
  auto context_provider = TestContextProvider::Create();
  context_provider->BindToCurrentThread();
  TestGpuImageDecodeCache cache(context_provider.get());
  sk_sp<SkImage> image = CreateImage(100, 100);
  bool is_decomposable = true;
  SkMatrix matrix = CreateMatrix(SkSize::Make(0.4f, 0.4f), is_decomposable);

  DrawImage draw_image(image, SkIRect::MakeWH(image->width(), image->height()),
                       kHigh_SkFilterQuality, matrix, DefaultColorSpace());
  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(task);

  DrawImage another_draw_image(
      image, SkIRect::MakeWH(image->width(), image->height()),
      kLow_SkFilterQuality, matrix, DefaultColorSpace());
  scoped_refptr<TileTask> another_task;
  need_unref = cache.GetTaskForImageAndRef(
      another_draw_image, ImageDecodeCache::TracingInfo(), &another_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(task.get() == another_task.get());

  TestTileTaskRunner::ProcessTask(task->dependencies()[0].get());
  TestTileTaskRunner::ProcessTask(task.get());

  cache.UnrefImage(draw_image);
  cache.UnrefImage(another_draw_image);
}

// crbug.com/709341.
#if defined(MEMORY_SANITIZER)
#define MAYBE_GetTaskForImageDifferentImage \
  DISABLED_GetTaskForImageDifferentImage
#else
#define MAYBE_GetTaskForImageDifferentImage GetTaskForImageDifferentImage
#endif
TEST(GpuImageDecodeCacheTest, MAYBE_GetTaskForImageDifferentImage) {
  auto context_provider = TestContextProvider::Create();
  context_provider->BindToCurrentThread();
  TestGpuImageDecodeCache cache(context_provider.get());
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  sk_sp<SkImage> first_image = CreateImage(100, 100);
  DrawImage first_draw_image(
      first_image, SkIRect::MakeWH(first_image->width(), first_image->height()),
      quality, CreateMatrix(SkSize::Make(0.5f, 0.5f), is_decomposable),
      DefaultColorSpace());
  scoped_refptr<TileTask> first_task;
  bool need_unref = cache.GetTaskForImageAndRef(
      first_draw_image, ImageDecodeCache::TracingInfo(), &first_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(first_task);

  sk_sp<SkImage> second_image = CreateImage(100, 100);
  DrawImage second_draw_image(
      second_image,
      SkIRect::MakeWH(second_image->width(), second_image->height()), quality,
      CreateMatrix(SkSize::Make(0.25f, 0.25f), is_decomposable),
      DefaultColorSpace());
  scoped_refptr<TileTask> second_task;
  need_unref = cache.GetTaskForImageAndRef(
      second_draw_image, ImageDecodeCache::TracingInfo(), &second_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(second_task);
  EXPECT_TRUE(first_task.get() != second_task.get());

  TestTileTaskRunner::ProcessTask(first_task->dependencies()[0].get());
  TestTileTaskRunner::ProcessTask(first_task.get());
  TestTileTaskRunner::ProcessTask(second_task->dependencies()[0].get());
  TestTileTaskRunner::ProcessTask(second_task.get());

  cache.UnrefImage(first_draw_image);
  cache.UnrefImage(second_draw_image);
}

// crbug.com/709341.
#if defined(MEMORY_SANITIZER)
#define MAYBE_GetTaskForImageLargerScale DISABLED_GetTaskForImageLargerScale
#else
#define MAYBE_GetTaskForImageLargerScale GetTaskForImageLargerScale
#endif
TEST(GpuImageDecodeCacheTest, MAYBE_GetTaskForImageLargerScale) {
  auto context_provider = TestContextProvider::Create();
  context_provider->BindToCurrentThread();
  TestGpuImageDecodeCache cache(context_provider.get());
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  sk_sp<SkImage> first_image = CreateImage(100, 100);
  DrawImage first_draw_image(
      first_image, SkIRect::MakeWH(first_image->width(), first_image->height()),
      quality, CreateMatrix(SkSize::Make(0.5f, 0.5f), is_decomposable),
      DefaultColorSpace());
  scoped_refptr<TileTask> first_task;
  bool need_unref = cache.GetTaskForImageAndRef(
      first_draw_image, ImageDecodeCache::TracingInfo(), &first_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(first_task);

  TestTileTaskRunner::ProcessTask(first_task->dependencies()[0].get());
  TestTileTaskRunner::ProcessTask(first_task.get());

  cache.UnrefImage(first_draw_image);

  DrawImage second_draw_image(
      first_image, SkIRect::MakeWH(first_image->width(), first_image->height()),
      quality, CreateMatrix(SkSize::Make(1.0f, 1.0f), is_decomposable),
      DefaultColorSpace());
  scoped_refptr<TileTask> second_task;
  need_unref = cache.GetTaskForImageAndRef(
      second_draw_image, ImageDecodeCache::TracingInfo(), &second_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(second_task);
  EXPECT_TRUE(first_task.get() != second_task.get());

  DrawImage third_draw_image(
      first_image, SkIRect::MakeWH(first_image->width(), first_image->height()),
      quality, CreateMatrix(SkSize::Make(0.5f, 0.5f), is_decomposable),
      DefaultColorSpace());
  scoped_refptr<TileTask> third_task;
  need_unref = cache.GetTaskForImageAndRef(
      third_draw_image, ImageDecodeCache::TracingInfo(), &third_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(third_task.get() == second_task.get());

  TestTileTaskRunner::ProcessTask(second_task->dependencies()[0].get());
  TestTileTaskRunner::ProcessTask(second_task.get());

  cache.UnrefImage(second_draw_image);
  cache.UnrefImage(third_draw_image);
}

// crbug.com/709341.
#if defined(MEMORY_SANITIZER)
#define MAYBE_GetTaskForImageLargerScaleNoReuse \
  DISABLED_GetTaskForImageLargerScaleNoReuse
#else
#define MAYBE_GetTaskForImageLargerScaleNoReuse \
  GetTaskForImageLargerScaleNoReuse
#endif
TEST(GpuImageDecodeCacheTest, MAYBE_GetTaskForImageLargerScaleNoReuse) {
  auto context_provider = TestContextProvider::Create();
  context_provider->BindToCurrentThread();
  TestGpuImageDecodeCache cache(context_provider.get());
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  sk_sp<SkImage> first_image = CreateImage(100, 100);
  DrawImage first_draw_image(
      first_image, SkIRect::MakeWH(first_image->width(), first_image->height()),
      quality, CreateMatrix(SkSize::Make(0.5f, 0.5f), is_decomposable),
      DefaultColorSpace());
  scoped_refptr<TileTask> first_task;
  bool need_unref = cache.GetTaskForImageAndRef(
      first_draw_image, ImageDecodeCache::TracingInfo(), &first_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(first_task);

  DrawImage second_draw_image(
      first_image, SkIRect::MakeWH(first_image->width(), first_image->height()),
      quality, CreateMatrix(SkSize::Make(1.0f, 1.0f), is_decomposable),
      DefaultColorSpace());
  scoped_refptr<TileTask> second_task;
  need_unref = cache.GetTaskForImageAndRef(
      second_draw_image, ImageDecodeCache::TracingInfo(), &second_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(second_task);
  EXPECT_TRUE(first_task.get() != second_task.get());

  DrawImage third_draw_image(
      first_image, SkIRect::MakeWH(first_image->width(), first_image->height()),
      quality, CreateMatrix(SkSize::Make(0.5f, 0.5f), is_decomposable),
      DefaultColorSpace());
  scoped_refptr<TileTask> third_task;
  need_unref = cache.GetTaskForImageAndRef(
      third_draw_image, ImageDecodeCache::TracingInfo(), &third_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(third_task.get() == first_task.get());

  TestTileTaskRunner::ProcessTask(first_task->dependencies()[0].get());
  TestTileTaskRunner::ProcessTask(first_task.get());
  TestTileTaskRunner::ProcessTask(second_task->dependencies()[0].get());
  TestTileTaskRunner::ProcessTask(second_task.get());

  cache.UnrefImage(first_draw_image);
  cache.UnrefImage(second_draw_image);
  cache.UnrefImage(third_draw_image);
}

// crbug.com/709341.
#if defined(MEMORY_SANITIZER)
#define MAYBE_GetTaskForImageHigherQuality DISABLED_GetTaskForImageHigherQuality
#else
#define MAYBE_GetTaskForImageHigherQuality GetTaskForImageHigherQuality
#endif
TEST(GpuImageDecodeCacheTest, MAYBE_GetTaskForImageHigherQuality) {
  auto context_provider = TestContextProvider::Create();
  context_provider->BindToCurrentThread();
  TestGpuImageDecodeCache cache(context_provider.get());
  bool is_decomposable = true;
  SkMatrix matrix = CreateMatrix(SkSize::Make(0.4f, 0.4f), is_decomposable);

  sk_sp<SkImage> first_image = CreateImage(100, 100);
  DrawImage first_draw_image(
      first_image, SkIRect::MakeWH(first_image->width(), first_image->height()),
      kLow_SkFilterQuality, matrix, DefaultColorSpace());
  scoped_refptr<TileTask> first_task;
  bool need_unref = cache.GetTaskForImageAndRef(
      first_draw_image, ImageDecodeCache::TracingInfo(), &first_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(first_task);

  TestTileTaskRunner::ProcessTask(first_task->dependencies()[0].get());
  TestTileTaskRunner::ProcessTask(first_task.get());

  cache.UnrefImage(first_draw_image);

  DrawImage second_draw_image(
      first_image, SkIRect::MakeWH(first_image->width(), first_image->height()),
      kHigh_SkFilterQuality, matrix, DefaultColorSpace());
  scoped_refptr<TileTask> second_task;
  need_unref = cache.GetTaskForImageAndRef(
      second_draw_image, ImageDecodeCache::TracingInfo(), &second_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(second_task);
  EXPECT_TRUE(first_task.get() != second_task.get());

  TestTileTaskRunner::ProcessTask(second_task->dependencies()[0].get());
  TestTileTaskRunner::ProcessTask(second_task.get());

  cache.UnrefImage(second_draw_image);
}

// crbug.com/709341.
#if defined(MEMORY_SANITIZER)
#define MAYBE_GetTaskForImageAlreadyDecodedAndLocked \
  DISABLED_GetTaskForImageAlreadyDecodedAndLocked
#else
#define MAYBE_GetTaskForImageAlreadyDecodedAndLocked \
  GetTaskForImageAlreadyDecodedAndLocked
#endif
TEST(GpuImageDecodeCacheTest, MAYBE_GetTaskForImageAlreadyDecodedAndLocked) {
  auto context_provider = TestContextProvider::Create();
  context_provider->BindToCurrentThread();
  TestGpuImageDecodeCache cache(context_provider.get());
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  sk_sp<SkImage> image = CreateImage(100, 100);
  DrawImage draw_image(image, SkIRect::MakeWH(image->width(), image->height()),
                       quality,
                       CreateMatrix(SkSize::Make(0.5f, 0.5f), is_decomposable),
                       DefaultColorSpace());
  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(task);
  EXPECT_EQ(task->dependencies().size(), 1u);
  EXPECT_TRUE(task->dependencies()[0]);

  // Run the decode but don't complete it (this will keep the decode locked).
  TestTileTaskRunner::ScheduleTask(task->dependencies()[0].get());
  TestTileTaskRunner::RunTask(task->dependencies()[0].get());

  // Cancel the upload.
  TestTileTaskRunner::CancelTask(task.get());
  TestTileTaskRunner::CompleteTask(task.get());

  // Get the image again - we should have an upload task, but no dependent
  // decode task, as the decode was already locked.
  scoped_refptr<TileTask> another_task;
  need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &another_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(another_task);
  EXPECT_EQ(another_task->dependencies().size(), 0u);

  TestTileTaskRunner::ProcessTask(another_task.get());

  // Finally, complete the original decode task.
  TestTileTaskRunner::CompleteTask(task->dependencies()[0].get());

  cache.UnrefImage(draw_image);
  cache.UnrefImage(draw_image);
}

// crbug.com/709341.
#if defined(MEMORY_SANITIZER)
#define MAYBE_GetTaskForImageAlreadyDecodedNotLocked \
  DISABLED_GetTaskForImageAlreadyDecodedNotLocked
#else
#define MAYBE_GetTaskForImageAlreadyDecodedNotLocked \
  GetTaskForImageAlreadyDecodedNotLocked
#endif
TEST(GpuImageDecodeCacheTest, MAYBE_GetTaskForImageAlreadyDecodedNotLocked) {
  auto context_provider = TestContextProvider::Create();
  context_provider->BindToCurrentThread();
  TestGpuImageDecodeCache cache(context_provider.get());
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  sk_sp<SkImage> image = CreateImage(100, 100);
  DrawImage draw_image(image, SkIRect::MakeWH(image->width(), image->height()),
                       quality,
                       CreateMatrix(SkSize::Make(0.5f, 0.5f), is_decomposable),
                       DefaultColorSpace());
  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(task);
  EXPECT_EQ(task->dependencies().size(), 1u);
  EXPECT_TRUE(task->dependencies()[0]);

  // Run the decode.
  TestTileTaskRunner::ProcessTask(task->dependencies()[0].get());

  // Cancel the upload.
  TestTileTaskRunner::CancelTask(task.get());
  TestTileTaskRunner::CompleteTask(task.get());

  // Unref the image.
  cache.UnrefImage(draw_image);

  // Get the image again - we should have an upload task and a dependent decode
  // task - this dependent task will typically just re-lock the image.
  scoped_refptr<TileTask> another_task;
  need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &another_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(another_task);
  EXPECT_EQ(another_task->dependencies().size(), 1u);
  EXPECT_TRUE(task->dependencies()[0]);

  TestTileTaskRunner::ProcessTask(another_task->dependencies()[0].get());
  TestTileTaskRunner::ProcessTask(another_task.get());

  cache.UnrefImage(draw_image);
}

// crbug.com/709341.
#if defined(MEMORY_SANITIZER)
#define MAYBE_GetTaskForImageAlreadyUploaded \
  DISABLED_GetTaskForImageAlreadyUploaded
#else
#define MAYBE_GetTaskForImageAlreadyUploaded GetTaskForImageAlreadyUploaded
#endif
TEST(GpuImageDecodeCacheTest, MAYBE_GetTaskForImageAlreadyUploaded) {
  auto context_provider = TestContextProvider::Create();
  context_provider->BindToCurrentThread();
  TestGpuImageDecodeCache cache(context_provider.get());
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  sk_sp<SkImage> image = CreateImage(100, 100);
  DrawImage draw_image(image, SkIRect::MakeWH(image->width(), image->height()),
                       quality,
                       CreateMatrix(SkSize::Make(0.5f, 0.5f), is_decomposable),
                       DefaultColorSpace());
  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(task);
  EXPECT_EQ(task->dependencies().size(), 1u);
  EXPECT_TRUE(task->dependencies()[0]);

  TestTileTaskRunner::ProcessTask(task->dependencies()[0].get());
  TestTileTaskRunner::ScheduleTask(task.get());
  TestTileTaskRunner::RunTask(task.get());

  scoped_refptr<TileTask> another_task;
  need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &another_task);
  EXPECT_TRUE(need_unref);
  EXPECT_FALSE(another_task);

  TestTileTaskRunner::CompleteTask(task.get());

  cache.UnrefImage(draw_image);
  cache.UnrefImage(draw_image);
}

// crbug.com/709341.
#if defined(MEMORY_SANITIZER)
#define MAYBE_GetTaskForImageCanceledGetsNewTask \
  DISABLED_GetTaskForImageCanceledGetsNewTask
#else
#define MAYBE_GetTaskForImageCanceledGetsNewTask \
  GetTaskForImageCanceledGetsNewTask
#endif
TEST(GpuImageDecodeCacheTest, MAYBE_GetTaskForImageCanceledGetsNewTask) {
  auto context_provider = TestContextProvider::Create();
  context_provider->BindToCurrentThread();
  TestGpuImageDecodeCache cache(context_provider.get());
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  sk_sp<SkImage> image = CreateImage(100, 100);
  DrawImage draw_image(image, SkIRect::MakeWH(image->width(), image->height()),
                       quality,
                       CreateMatrix(SkSize::Make(0.5f, 0.5f), is_decomposable),
                       DefaultColorSpace());
  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(task);

  TestTileTaskRunner::ProcessTask(task->dependencies()[0].get());

  scoped_refptr<TileTask> another_task;
  need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &another_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(another_task.get() == task.get());

  // Didn't run the task, so cancel it.
  TestTileTaskRunner::CancelTask(task.get());
  TestTileTaskRunner::CompleteTask(task.get());

  // Fully cancel everything (so the raster would unref things).
  cache.UnrefImage(draw_image);
  cache.UnrefImage(draw_image);

  // Here a new task is created.
  scoped_refptr<TileTask> third_task;
  need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &third_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(third_task);
  EXPECT_FALSE(third_task.get() == task.get());

  TestTileTaskRunner::ProcessTask(third_task->dependencies()[0].get());
  TestTileTaskRunner::ProcessTask(third_task.get());

  cache.UnrefImage(draw_image);
}

// crbug.com/709341.
#if defined(MEMORY_SANITIZER)
#define MAYBE_GetTaskForImageCanceledWhileReffedGetsNewTask \
  DISABLED_GetTaskForImageCanceledWhileReffedGetsNewTask
#else
#define MAYBE_GetTaskForImageCanceledWhileReffedGetsNewTask \
  GetTaskForImageCanceledWhileReffedGetsNewTask
#endif
TEST(GpuImageDecodeCacheTest,
     MAYBE_GetTaskForImageCanceledWhileReffedGetsNewTask) {
  auto context_provider = TestContextProvider::Create();
  context_provider->BindToCurrentThread();
  TestGpuImageDecodeCache cache(context_provider.get());
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  sk_sp<SkImage> image = CreateImage(100, 100);
  DrawImage draw_image(image, SkIRect::MakeWH(image->width(), image->height()),
                       quality,
                       CreateMatrix(SkSize::Make(0.5f, 0.5f), is_decomposable),
                       DefaultColorSpace());
  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(task);

  ASSERT_GT(task->dependencies().size(), 0u);
  TestTileTaskRunner::ProcessTask(task->dependencies()[0].get());

  scoped_refptr<TileTask> another_task;
  need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &another_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(another_task.get() == task.get());

  // Didn't run the task, so cancel it.
  TestTileTaskRunner::CancelTask(task.get());
  TestTileTaskRunner::CompleteTask(task.get());

  // 2 Unrefs, so that the decode is unlocked as well.
  cache.UnrefImage(draw_image);
  cache.UnrefImage(draw_image);

  // Note that here, everything is reffed, but a new task is created. This is
  // possible with repeated schedule/cancel operations.
  scoped_refptr<TileTask> third_task;
  need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &third_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(third_task);
  EXPECT_FALSE(third_task.get() == task.get());

  ASSERT_GT(third_task->dependencies().size(), 0u);
  TestTileTaskRunner::ProcessTask(third_task->dependencies()[0].get());
  TestTileTaskRunner::ProcessTask(third_task.get());

  // Unref!
  cache.UnrefImage(draw_image);
}

// crbug.com/709341.
#if defined(MEMORY_SANITIZER)
#define MAYBE_NoTaskForImageAlreadyFailedDecoding \
  DISABLED_NoTaskForImageAlreadyFailedDecoding
#else
#define MAYBE_NoTaskForImageAlreadyFailedDecoding \
  NoTaskForImageAlreadyFailedDecoding
#endif
TEST(GpuImageDecodeCacheTest, MAYBE_NoTaskForImageAlreadyFailedDecoding) {
  auto context_provider = TestContextProvider::Create();
  context_provider->BindToCurrentThread();
  TestGpuImageDecodeCache cache(context_provider.get());
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  sk_sp<SkImage> image = CreateImage(100, 100);
  DrawImage draw_image(image, SkIRect::MakeWH(image->width(), image->height()),
                       quality,
                       CreateMatrix(SkSize::Make(0.5f, 0.5f), is_decomposable),
                       DefaultColorSpace());
  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(task);

  TestTileTaskRunner::ProcessTask(task->dependencies()[0].get());
  // Didn't run the task, so cancel it.
  TestTileTaskRunner::CancelTask(task.get());
  TestTileTaskRunner::CompleteTask(task.get());

  cache.SetImageDecodingFailedForTesting(draw_image);

  scoped_refptr<TileTask> another_task;
  need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &another_task);
  EXPECT_FALSE(need_unref);
  EXPECT_EQ(another_task.get(), nullptr);

  cache.UnrefImage(draw_image);
}

// crbug.com/709341.
#if defined(MEMORY_SANITIZER)
#define MAYBE_GetDecodedImageForDraw DISABLED_GetDecodedImageForDraw
#else
#define MAYBE_GetDecodedImageForDraw GetDecodedImageForDraw
#endif
TEST(GpuImageDecodeCacheTest, MAYBE_GetDecodedImageForDraw) {
  auto context_provider = TestContextProvider::Create();
  context_provider->BindToCurrentThread();
  TestGpuImageDecodeCache cache(context_provider.get());
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  sk_sp<SkImage> image = CreateImage(100, 100);
  DrawImage draw_image(image, SkIRect::MakeWH(image->width(), image->height()),
                       quality,
                       CreateMatrix(SkSize::Make(0.5f, 0.5f), is_decomposable),
                       DefaultColorSpace());
  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(task);

  TestTileTaskRunner::ProcessTask(task->dependencies()[0].get());
  TestTileTaskRunner::ProcessTask(task.get());

  // Must hold context lock before calling GetDecodedImageForDraw /
  // DrawWithImageFinished.
  ContextProvider::ScopedContextLock context_lock(context_provider.get());
  DecodedDrawImage decoded_draw_image =
      cache.GetDecodedImageForDraw(draw_image);
  EXPECT_TRUE(decoded_draw_image.image());
  EXPECT_TRUE(decoded_draw_image.image()->isTextureBacked());
  EXPECT_FALSE(decoded_draw_image.is_at_raster_decode());
  EXPECT_FALSE(cache.DiscardableIsLockedForTesting(draw_image));

  cache.DrawWithImageFinished(draw_image, decoded_draw_image);
  cache.UnrefImage(draw_image);
}

TEST(GpuImageDecodeCacheTest, GetLargeDecodedImageForDraw) {
  auto context_provider = TestContextProvider::Create();
  context_provider->BindToCurrentThread();
  TestGpuImageDecodeCache cache(context_provider.get());
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  sk_sp<SkImage> image = CreateImage(1, 24000);
  DrawImage draw_image(image, SkIRect::MakeWH(image->width(), image->height()),
                       quality,
                       CreateMatrix(SkSize::Make(1.0f, 1.0f), is_decomposable),
                       DefaultColorSpace());
  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(task);

  TestTileTaskRunner::ProcessTask(task->dependencies()[0].get());
  TestTileTaskRunner::ProcessTask(task.get());

  // Must hold context lock before calling GetDecodedImageForDraw /
  // DrawWithImageFinished.
  ContextProvider::ScopedContextLock context_lock(context_provider.get());
  DecodedDrawImage decoded_draw_image =
      cache.GetDecodedImageForDraw(draw_image);
  EXPECT_TRUE(decoded_draw_image.image());
  EXPECT_FALSE(decoded_draw_image.image()->isTextureBacked());
  EXPECT_FALSE(decoded_draw_image.is_at_raster_decode());
  EXPECT_TRUE(cache.DiscardableIsLockedForTesting(draw_image));

  cache.DrawWithImageFinished(draw_image, decoded_draw_image);
  cache.UnrefImage(draw_image);
  EXPECT_FALSE(cache.DiscardableIsLockedForTesting(draw_image));
}

TEST(GpuImageDecodeCacheTest, GetDecodedImageForDrawAtRasterDecode) {
  auto context_provider = TestContextProvider::Create();
  context_provider->BindToCurrentThread();
  TestGpuImageDecodeCache cache(context_provider.get());
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  cache.SetAllByteLimitsForTesting(0);

  sk_sp<SkImage> image = CreateImage(100, 100);
  DrawImage draw_image(image, SkIRect::MakeWH(image->width(), image->height()),
                       quality,
                       CreateMatrix(SkSize::Make(1.0f, 1.0f), is_decomposable),
                       DefaultColorSpace());

  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_FALSE(need_unref);
  EXPECT_FALSE(task);

  // Must hold context lock before calling GetDecodedImageForDraw /
  // DrawWithImageFinished.
  ContextProvider::ScopedContextLock context_lock(context_provider.get());
  DecodedDrawImage decoded_draw_image =
      cache.GetDecodedImageForDraw(draw_image);
  EXPECT_TRUE(decoded_draw_image.image());
  EXPECT_TRUE(decoded_draw_image.image()->isTextureBacked());
  EXPECT_TRUE(decoded_draw_image.is_at_raster_decode());
  EXPECT_FALSE(cache.DiscardableIsLockedForTesting(draw_image));

  cache.DrawWithImageFinished(draw_image, decoded_draw_image);
}

// crbug.com/709341.
#if defined(MEMORY_SANITIZER)
#define MAYBE_GetDecodedImageForDrawLargerScale \
  DISABLED_GetDecodedImageForDrawLargerScale
#else
#define MAYBE_GetDecodedImageForDrawLargerScale \
  GetDecodedImageForDrawLargerScale
#endif
TEST(GpuImageDecodeCacheTest, MAYBE_GetDecodedImageForDrawLargerScale) {
  auto context_provider = TestContextProvider::Create();
  context_provider->BindToCurrentThread();
  TestGpuImageDecodeCache cache(context_provider.get());
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  sk_sp<SkImage> image = CreateImage(100, 100);
  DrawImage draw_image(image, SkIRect::MakeWH(image->width(), image->height()),
                       quality,
                       CreateMatrix(SkSize::Make(0.5f, 0.5f), is_decomposable),
                       DefaultColorSpace());
  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(task);

  TestTileTaskRunner::ProcessTask(task->dependencies()[0].get());
  TestTileTaskRunner::ProcessTask(task.get());

  DrawImage larger_draw_image(
      image, SkIRect::MakeWH(image->width(), image->height()), quality,
      CreateMatrix(SkSize::Make(1.5f, 1.5f), is_decomposable),
      DefaultColorSpace());
  scoped_refptr<TileTask> larger_task;
  bool larger_need_unref = cache.GetTaskForImageAndRef(
      larger_draw_image, ImageDecodeCache::TracingInfo(), &larger_task);
  EXPECT_TRUE(larger_need_unref);
  EXPECT_TRUE(larger_task);

  TestTileTaskRunner::ProcessTask(larger_task->dependencies()[0].get());
  TestTileTaskRunner::ProcessTask(larger_task.get());

  // Must hold context lock before calling GetDecodedImageForDraw /
  // DrawWithImageFinished.
  ContextProvider::ScopedContextLock context_lock(context_provider.get());
  DecodedDrawImage decoded_draw_image =
      cache.GetDecodedImageForDraw(draw_image);
  EXPECT_TRUE(decoded_draw_image.image());
  EXPECT_TRUE(decoded_draw_image.image()->isTextureBacked());
  EXPECT_FALSE(decoded_draw_image.is_at_raster_decode());
  EXPECT_FALSE(cache.DiscardableIsLockedForTesting(draw_image));

  DecodedDrawImage larger_decoded_draw_image =
      cache.GetDecodedImageForDraw(larger_draw_image);
  EXPECT_TRUE(larger_decoded_draw_image.image());
  EXPECT_TRUE(larger_decoded_draw_image.image()->isTextureBacked());
  EXPECT_FALSE(larger_decoded_draw_image.is_at_raster_decode());
  EXPECT_FALSE(cache.DiscardableIsLockedForTesting(draw_image));

  EXPECT_FALSE(decoded_draw_image.image() == larger_decoded_draw_image.image());

  cache.DrawWithImageFinished(draw_image, decoded_draw_image);
  cache.UnrefImage(draw_image);
  cache.DrawWithImageFinished(larger_draw_image, larger_decoded_draw_image);
  cache.UnrefImage(larger_draw_image);
}

// crbug.com/709341.
#if defined(MEMORY_SANITIZER)
#define MAYBE_GetDecodedImageForDrawHigherQuality \
  DISABLED_GetDecodedImageForDrawHigherQuality
#else
#define MAYBE_GetDecodedImageForDrawHigherQuality \
  GetDecodedImageForDrawHigherQuality
#endif
TEST(GpuImageDecodeCacheTest, MAYBE_GetDecodedImageForDrawHigherQuality) {
  auto context_provider = TestContextProvider::Create();
  context_provider->BindToCurrentThread();
  TestGpuImageDecodeCache cache(context_provider.get());
  bool is_decomposable = true;
  SkMatrix matrix = CreateMatrix(SkSize::Make(0.5f, 0.5f), is_decomposable);

  sk_sp<SkImage> image = CreateImage(100, 100);
  DrawImage draw_image(image, SkIRect::MakeWH(image->width(), image->height()),
                       kLow_SkFilterQuality, matrix, DefaultColorSpace());
  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(task);

  TestTileTaskRunner::ProcessTask(task->dependencies()[0].get());
  TestTileTaskRunner::ProcessTask(task.get());

  DrawImage higher_quality_draw_image(
      image, SkIRect::MakeWH(image->width(), image->height()),
      kHigh_SkFilterQuality, matrix, DefaultColorSpace());
  scoped_refptr<TileTask> hq_task;
  bool hq_needs_unref = cache.GetTaskForImageAndRef(
      higher_quality_draw_image, ImageDecodeCache::TracingInfo(), &hq_task);
  EXPECT_TRUE(hq_needs_unref);
  EXPECT_TRUE(hq_task);

  TestTileTaskRunner::ProcessTask(hq_task->dependencies()[0].get());
  TestTileTaskRunner::ProcessTask(hq_task.get());

  // Must hold context lock before calling GetDecodedImageForDraw /
  // DrawWithImageFinished.
  ContextProvider::ScopedContextLock context_lock(context_provider.get());
  DecodedDrawImage decoded_draw_image =
      cache.GetDecodedImageForDraw(draw_image);
  EXPECT_TRUE(decoded_draw_image.image());
  EXPECT_TRUE(decoded_draw_image.image()->isTextureBacked());
  EXPECT_FALSE(decoded_draw_image.is_at_raster_decode());
  EXPECT_FALSE(cache.DiscardableIsLockedForTesting(draw_image));

  DecodedDrawImage larger_decoded_draw_image =
      cache.GetDecodedImageForDraw(higher_quality_draw_image);
  EXPECT_TRUE(larger_decoded_draw_image.image());
  EXPECT_TRUE(larger_decoded_draw_image.image()->isTextureBacked());
  EXPECT_FALSE(larger_decoded_draw_image.is_at_raster_decode());
  EXPECT_FALSE(cache.DiscardableIsLockedForTesting(draw_image));

  EXPECT_FALSE(decoded_draw_image.image() == larger_decoded_draw_image.image());

  cache.DrawWithImageFinished(draw_image, decoded_draw_image);
  cache.UnrefImage(draw_image);
  cache.DrawWithImageFinished(higher_quality_draw_image,
                              larger_decoded_draw_image);
  cache.UnrefImage(higher_quality_draw_image);
}

// crbug.com/709341.
#if defined(MEMORY_SANITIZER)
#define MAYBE_GetDecodedImageForDrawNegative \
  DISABLED_GetDecodedImageForDrawNegative
#else
#define MAYBE_GetDecodedImageForDrawNegative GetDecodedImageForDrawNegative
#endif
TEST(GpuImageDecodeCacheTest, MAYBE_GetDecodedImageForDrawNegative) {
  auto context_provider = TestContextProvider::Create();
  context_provider->BindToCurrentThread();
  TestGpuImageDecodeCache cache(context_provider.get());
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  sk_sp<SkImage> image = CreateImage(100, 100);
  DrawImage draw_image(image, SkIRect::MakeWH(image->width(), image->height()),
                       quality,
                       CreateMatrix(SkSize::Make(-0.5f, 0.5f), is_decomposable),
                       DefaultColorSpace());
  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(task);

  TestTileTaskRunner::ProcessTask(task->dependencies()[0].get());
  TestTileTaskRunner::ProcessTask(task.get());

  // Must hold context lock before calling GetDecodedImageForDraw /
  // DrawWithImageFinished.
  ContextProvider::ScopedContextLock context_lock(context_provider.get());
  DecodedDrawImage decoded_draw_image =
      cache.GetDecodedImageForDraw(draw_image);
  EXPECT_TRUE(decoded_draw_image.image());
  EXPECT_EQ(decoded_draw_image.image()->width(), 50);
  EXPECT_EQ(decoded_draw_image.image()->height(), 50);
  EXPECT_TRUE(decoded_draw_image.image()->isTextureBacked());
  EXPECT_FALSE(decoded_draw_image.is_at_raster_decode());
  EXPECT_FALSE(cache.DiscardableIsLockedForTesting(draw_image));

  cache.DrawWithImageFinished(draw_image, decoded_draw_image);
  cache.UnrefImage(draw_image);
}

// crbug.com/709341.
#if defined(MEMORY_SANITIZER)
#define MAYBE_GetLargeScaledDecodedImageForDraw \
  DISABLED_GetLargeScaledDecodedImageForDraw
#else
#define MAYBE_GetLargeScaledDecodedImageForDraw \
  GetLargeScaledDecodedImageForDraw
#endif
TEST(GpuImageDecodeCacheTest, MAYBE_GetLargeScaledDecodedImageForDraw) {
  auto context_provider = TestContextProvider::Create();
  context_provider->BindToCurrentThread();
  TestGpuImageDecodeCache cache(context_provider.get());
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  sk_sp<SkImage> image = CreateImage(1, 48000);
  DrawImage draw_image(image, SkIRect::MakeWH(image->width(), image->height()),
                       quality,
                       CreateMatrix(SkSize::Make(0.5f, 0.5f), is_decomposable),
                       DefaultColorSpace());
  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(task);

  TestTileTaskRunner::ProcessTask(task->dependencies()[0].get());
  TestTileTaskRunner::ProcessTask(task.get());

  // Must hold context lock before calling GetDecodedImageForDraw /
  // DrawWithImageFinished.
  ContextProvider::ScopedContextLock context_lock(context_provider.get());
  DecodedDrawImage decoded_draw_image =
      cache.GetDecodedImageForDraw(draw_image);
  EXPECT_TRUE(decoded_draw_image.image());
  // The mip level scale should never go below 0 in any dimension.
  EXPECT_EQ(1, decoded_draw_image.image()->width());
  EXPECT_EQ(24000, decoded_draw_image.image()->height());
  EXPECT_FALSE(decoded_draw_image.image()->isTextureBacked());
  EXPECT_FALSE(decoded_draw_image.is_at_raster_decode());
  EXPECT_TRUE(cache.DiscardableIsLockedForTesting(draw_image));

  cache.DrawWithImageFinished(draw_image, decoded_draw_image);
  cache.UnrefImage(draw_image);
  EXPECT_FALSE(cache.DiscardableIsLockedForTesting(draw_image));
}

// crbug.com/709341.
#if defined(MEMORY_SANITIZER)
#define MAYBE_AtRasterUsedDirectlyIfSpaceAllows \
  DISABLED_AtRasterUsedDirectlyIfSpaceAllows
#else
#define MAYBE_AtRasterUsedDirectlyIfSpaceAllows \
  AtRasterUsedDirectlyIfSpaceAllows
#endif
TEST(GpuImageDecodeCacheTest, MAYBE_AtRasterUsedDirectlyIfSpaceAllows) {
  auto context_provider = TestContextProvider::Create();
  context_provider->BindToCurrentThread();
  TestGpuImageDecodeCache cache(context_provider.get());
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  cache.SetAllByteLimitsForTesting(0);

  sk_sp<SkImage> image = CreateImage(100, 100);
  DrawImage draw_image(image, SkIRect::MakeWH(image->width(), image->height()),
                       quality,
                       CreateMatrix(SkSize::Make(0.5f, 0.5f), is_decomposable),
                       DefaultColorSpace());

  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_FALSE(need_unref);
  EXPECT_FALSE(task);

  // Must hold context lock before calling GetDecodedImageForDraw /
  // DrawWithImageFinished.
  ContextProvider::ScopedContextLock context_lock(context_provider.get());
  DecodedDrawImage decoded_draw_image =
      cache.GetDecodedImageForDraw(draw_image);
  EXPECT_TRUE(decoded_draw_image.image());
  EXPECT_TRUE(decoded_draw_image.image()->isTextureBacked());
  EXPECT_TRUE(decoded_draw_image.is_at_raster_decode());
  EXPECT_FALSE(cache.DiscardableIsLockedForTesting(draw_image));

  cache.SetAllByteLimitsForTesting(96 * 1024 * 1024);

  // Finish our draw after increasing the memory limit, image should be added to
  // cache.
  cache.DrawWithImageFinished(draw_image, decoded_draw_image);

  scoped_refptr<TileTask> another_task;
  bool another_task_needs_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_TRUE(another_task_needs_unref);
  EXPECT_FALSE(another_task);
  cache.UnrefImage(draw_image);
}

// crbug.com/709341.
#if defined(MEMORY_SANITIZER)
#define MAYBE_GetDecodedImageForDrawAtRasterDecodeMultipleTimes \
  DISABLED_GetDecodedImageForDrawAtRasterDecodeMultipleTimes
#else
#define MAYBE_GetDecodedImageForDrawAtRasterDecodeMultipleTimes \
  GetDecodedImageForDrawAtRasterDecodeMultipleTimes
#endif
TEST(GpuImageDecodeCacheTest,
     MAYBE_GetDecodedImageForDrawAtRasterDecodeMultipleTimes) {
  auto context_provider = TestContextProvider::Create();
  context_provider->BindToCurrentThread();
  TestGpuImageDecodeCache cache(context_provider.get());
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  cache.SetAllByteLimitsForTesting(0);

  sk_sp<SkImage> image = CreateImage(100, 100);
  DrawImage draw_image(image, SkIRect::MakeWH(image->width(), image->height()),
                       quality,
                       CreateMatrix(SkSize::Make(0.5f, 0.5f), is_decomposable),
                       DefaultColorSpace());

  // Must hold context lock before calling GetDecodedImageForDraw /
  // DrawWithImageFinished.
  ContextProvider::ScopedContextLock context_lock(context_provider.get());
  DecodedDrawImage decoded_draw_image =
      cache.GetDecodedImageForDraw(draw_image);
  EXPECT_TRUE(decoded_draw_image.image());
  EXPECT_TRUE(decoded_draw_image.image()->isTextureBacked());
  EXPECT_TRUE(decoded_draw_image.is_at_raster_decode());
  EXPECT_FALSE(cache.DiscardableIsLockedForTesting(draw_image));

  DecodedDrawImage another_decoded_draw_image =
      cache.GetDecodedImageForDraw(draw_image);
  EXPECT_EQ(decoded_draw_image.image()->uniqueID(),
            another_decoded_draw_image.image()->uniqueID());

  cache.DrawWithImageFinished(draw_image, decoded_draw_image);
  cache.DrawWithImageFinished(draw_image, another_decoded_draw_image);
}

TEST(GpuImageDecodeCacheTest,
     GetLargeDecodedImageForDrawAtRasterDecodeMultipleTimes) {
  auto context_provider = TestContextProvider::Create();
  context_provider->BindToCurrentThread();
  TestGpuImageDecodeCache cache(context_provider.get());
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  sk_sp<SkImage> image = CreateImage(1, 24000);
  DrawImage draw_image(image, SkIRect::MakeWH(image->width(), image->height()),
                       quality,
                       CreateMatrix(SkSize::Make(1.0f, 1.0f), is_decomposable),
                       DefaultColorSpace());

  // Must hold context lock before calling GetDecodedImageForDraw /
  // DrawWithImageFinished.
  ContextProvider::ScopedContextLock context_lock(context_provider.get());
  DecodedDrawImage decoded_draw_image =
      cache.GetDecodedImageForDraw(draw_image);
  EXPECT_TRUE(decoded_draw_image.image());
  EXPECT_FALSE(decoded_draw_image.image()->isTextureBacked());
  EXPECT_TRUE(decoded_draw_image.is_at_raster_decode());
  EXPECT_TRUE(cache.DiscardableIsLockedForTesting(draw_image));

  cache.DrawWithImageFinished(draw_image, decoded_draw_image);
  EXPECT_FALSE(cache.DiscardableIsLockedForTesting(draw_image));

  DecodedDrawImage second_decoded_draw_image =
      cache.GetDecodedImageForDraw(draw_image);
  EXPECT_TRUE(second_decoded_draw_image.image());
  EXPECT_FALSE(second_decoded_draw_image.image()->isTextureBacked());
  EXPECT_TRUE(second_decoded_draw_image.is_at_raster_decode());
  EXPECT_TRUE(cache.DiscardableIsLockedForTesting(draw_image));

  cache.DrawWithImageFinished(draw_image, second_decoded_draw_image);
  EXPECT_FALSE(cache.DiscardableIsLockedForTesting(draw_image));
}

TEST(GpuImageDecodeCacheTest, ZeroSizedImagesAreSkipped) {
  auto context_provider = TestContextProvider::Create();
  context_provider->BindToCurrentThread();
  TestGpuImageDecodeCache cache(context_provider.get());
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  sk_sp<SkImage> image = CreateImage(100, 100);
  DrawImage draw_image(image, SkIRect::MakeWH(image->width(), image->height()),
                       quality,
                       CreateMatrix(SkSize::Make(0.f, 0.f), is_decomposable),
                       DefaultColorSpace());

  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_FALSE(task);
  EXPECT_FALSE(need_unref);

  // Must hold context lock before calling GetDecodedImageForDraw /
  // DrawWithImageFinished.
  ContextProvider::ScopedContextLock context_lock(context_provider.get());
  DecodedDrawImage decoded_draw_image =
      cache.GetDecodedImageForDraw(draw_image);
  EXPECT_FALSE(decoded_draw_image.image());

  cache.DrawWithImageFinished(draw_image, decoded_draw_image);
}

TEST(GpuImageDecodeCacheTest, NonOverlappingSrcRectImagesAreSkipped) {
  auto context_provider = TestContextProvider::Create();
  context_provider->BindToCurrentThread();
  TestGpuImageDecodeCache cache(context_provider.get());
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  sk_sp<SkImage> image = CreateImage(100, 100);
  DrawImage draw_image(
      image, SkIRect::MakeXYWH(150, 150, image->width(), image->height()),
      quality, CreateMatrix(SkSize::Make(1.f, 1.f), is_decomposable),
      DefaultColorSpace());

  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_FALSE(task);
  EXPECT_FALSE(need_unref);

  // Must hold context lock before calling GetDecodedImageForDraw /
  // DrawWithImageFinished.
  ContextProvider::ScopedContextLock context_lock(context_provider.get());
  DecodedDrawImage decoded_draw_image =
      cache.GetDecodedImageForDraw(draw_image);
  EXPECT_FALSE(decoded_draw_image.image());

  cache.DrawWithImageFinished(draw_image, decoded_draw_image);
}

TEST(GpuImageDecodeCacheTest, CanceledTasksDoNotCountAgainstBudget) {
  auto context_provider = TestContextProvider::Create();
  context_provider->BindToCurrentThread();
  TestGpuImageDecodeCache cache(context_provider.get());
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  sk_sp<SkImage> image = CreateImage(100, 100);
  DrawImage draw_image(
      image, SkIRect::MakeXYWH(0, 0, image->width(), image->height()), quality,
      CreateMatrix(SkSize::Make(1.f, 1.f), is_decomposable),
      DefaultColorSpace());

  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_NE(0u, cache.GetBytesUsedForTesting());
  EXPECT_TRUE(task);
  EXPECT_TRUE(need_unref);

  TestTileTaskRunner::CancelTask(task->dependencies()[0].get());
  TestTileTaskRunner::CompleteTask(task->dependencies()[0].get());
  TestTileTaskRunner::CancelTask(task.get());
  TestTileTaskRunner::CompleteTask(task.get());

  cache.UnrefImage(draw_image);
  EXPECT_EQ(0u, cache.GetBytesUsedForTesting());
}

// crbug.com/709341.
#if defined(MEMORY_SANITIZER)
#define MAYBE_ShouldAggressivelyFreeResources \
  DISABLED_ShouldAggressivelyFreeResources
#else
#define MAYBE_ShouldAggressivelyFreeResources ShouldAggressivelyFreeResources
#endif
TEST(GpuImageDecodeCacheTest, MAYBE_ShouldAggressivelyFreeResources) {
  auto context_provider = TestContextProvider::Create();
  context_provider->BindToCurrentThread();
  TestGpuImageDecodeCache cache(context_provider.get());
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  sk_sp<SkImage> image = CreateImage(100, 100);
  DrawImage draw_image(image, SkIRect::MakeWH(image->width(), image->height()),
                       quality,
                       CreateMatrix(SkSize::Make(0.5f, 0.5f), is_decomposable),
                       DefaultColorSpace());
  scoped_refptr<TileTask> task;
  {
    bool need_unref = cache.GetTaskForImageAndRef(
        draw_image, ImageDecodeCache::TracingInfo(), &task);
    EXPECT_TRUE(need_unref);
    EXPECT_TRUE(task);
  }

  TestTileTaskRunner::ProcessTask(task->dependencies()[0].get());
  TestTileTaskRunner::ProcessTask(task.get());

  cache.UnrefImage(draw_image);

  // We should now have data image in our cache.
  EXPECT_GT(cache.GetBytesUsedForTesting(), 0u);

  // Tell our cache to aggressively free resources.
  cache.SetShouldAggressivelyFreeResources(true);
  EXPECT_EQ(0u, cache.GetBytesUsedForTesting());

  // Attempting to upload a new image should succeed, but the image should not
  // be cached past its use.
  {
    bool need_unref = cache.GetTaskForImageAndRef(
        draw_image, ImageDecodeCache::TracingInfo(), &task);
    EXPECT_TRUE(need_unref);
    EXPECT_TRUE(task);

    TestTileTaskRunner::ProcessTask(task->dependencies()[0].get());
    TestTileTaskRunner::ProcessTask(task.get());
    cache.UnrefImage(draw_image);

    EXPECT_EQ(cache.GetBytesUsedForTesting(), 0u);
  }

  // We now tell the cache to not aggressively free resources. The image may
  // now be cached past its use.
  cache.SetShouldAggressivelyFreeResources(false);
  {
    bool need_unref = cache.GetTaskForImageAndRef(
        draw_image, ImageDecodeCache::TracingInfo(), &task);
    EXPECT_TRUE(need_unref);
    EXPECT_TRUE(task);

    TestTileTaskRunner::ProcessTask(task->dependencies()[0].get());
    TestTileTaskRunner::ProcessTask(task.get());
    cache.UnrefImage(draw_image);

    EXPECT_GT(cache.GetBytesUsedForTesting(), 0u);
  }
}

// crbug.com/709341.
#if defined(MEMORY_SANITIZER)
#define MAYBE_OrphanedImagesFreeOnReachingZeroRefs \
  DISABLED_OrphanedImagesFreeOnReachingZeroRefs
#else
#define MAYBE_OrphanedImagesFreeOnReachingZeroRefs \
  OrphanedImagesFreeOnReachingZeroRefs
#endif
TEST(GpuImageDecodeCacheTest, MAYBE_OrphanedImagesFreeOnReachingZeroRefs) {
  auto context_provider = TestContextProvider::Create();
  context_provider->BindToCurrentThread();
  TestGpuImageDecodeCache cache(context_provider.get());
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  // Create a downscaled image.
  sk_sp<SkImage> first_image = CreateImage(100, 100);
  DrawImage first_draw_image(
      first_image, SkIRect::MakeWH(first_image->width(), first_image->height()),
      quality, CreateMatrix(SkSize::Make(0.5f, 0.5f), is_decomposable),
      DefaultColorSpace());
  scoped_refptr<TileTask> first_task;
  bool need_unref = cache.GetTaskForImageAndRef(
      first_draw_image, ImageDecodeCache::TracingInfo(), &first_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(first_task);

  // The budget should account for exactly one image.
  EXPECT_EQ(cache.GetBytesUsedForTesting(),
            cache.GetDrawImageSizeForTesting(first_draw_image));

  // Create a larger version of |first_image|, this should immediately free the
  // memory used by |first_image| for the smaller scale.
  DrawImage second_draw_image(
      first_image, SkIRect::MakeWH(first_image->width(), first_image->height()),
      quality, CreateMatrix(SkSize::Make(1.0f, 1.0f), is_decomposable),
      DefaultColorSpace());
  scoped_refptr<TileTask> second_task;
  need_unref = cache.GetTaskForImageAndRef(
      second_draw_image, ImageDecodeCache::TracingInfo(), &second_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(second_task);
  EXPECT_TRUE(first_task.get() != second_task.get());

  TestTileTaskRunner::ProcessTask(second_task->dependencies()[0].get());
  TestTileTaskRunner::ProcessTask(second_task.get());

  cache.UnrefImage(second_draw_image);

  // The budget should account for both images one image.
  EXPECT_EQ(cache.GetBytesUsedForTesting(),
            cache.GetDrawImageSizeForTesting(second_draw_image) +
                cache.GetDrawImageSizeForTesting(first_draw_image));

  // Unref the first image, it was orphaned, so it should be immediately
  // deleted.
  TestTileTaskRunner::ProcessTask(first_task->dependencies()[0].get());
  TestTileTaskRunner::ProcessTask(first_task.get());
  cache.UnrefImage(first_draw_image);

  // The budget should account for exactly one image.
  EXPECT_EQ(cache.GetBytesUsedForTesting(),
            cache.GetDrawImageSizeForTesting(second_draw_image));
}

// crbug.com/709341.
#if defined(MEMORY_SANITIZER)
#define MAYBE_OrphanedZeroRefImagesImmediatelyDeleted \
  DISABLED_OrphanedZeroRefImagesImmediatelyDeleted
#else
#define MAYBE_OrphanedZeroRefImagesImmediatelyDeleted \
  OrphanedZeroRefImagesImmediatelyDeleted
#endif
TEST(GpuImageDecodeCacheTest, MAYBE_OrphanedZeroRefImagesImmediatelyDeleted) {
  auto context_provider = TestContextProvider::Create();
  context_provider->BindToCurrentThread();
  TestGpuImageDecodeCache cache(context_provider.get());
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  // Create a downscaled image.
  sk_sp<SkImage> first_image = CreateImage(100, 100);
  DrawImage first_draw_image(
      first_image, SkIRect::MakeWH(first_image->width(), first_image->height()),
      quality, CreateMatrix(SkSize::Make(0.5f, 0.5f), is_decomposable),
      DefaultColorSpace());
  scoped_refptr<TileTask> first_task;
  bool need_unref = cache.GetTaskForImageAndRef(
      first_draw_image, ImageDecodeCache::TracingInfo(), &first_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(first_task);

  TestTileTaskRunner::ProcessTask(first_task->dependencies()[0].get());
  TestTileTaskRunner::ProcessTask(first_task.get());
  cache.UnrefImage(first_draw_image);

  // The budget should account for exactly one image.
  EXPECT_EQ(cache.GetBytesUsedForTesting(),
            cache.GetDrawImageSizeForTesting(first_draw_image));

  // Create a larger version of |first_image|, this should immediately free the
  // memory used by |first_image| for the smaller scale.
  DrawImage second_draw_image(
      first_image, SkIRect::MakeWH(first_image->width(), first_image->height()),
      quality, CreateMatrix(SkSize::Make(1.0f, 1.0f), is_decomposable),
      DefaultColorSpace());
  scoped_refptr<TileTask> second_task;
  need_unref = cache.GetTaskForImageAndRef(
      second_draw_image, ImageDecodeCache::TracingInfo(), &second_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(second_task);
  EXPECT_TRUE(first_task.get() != second_task.get());

  TestTileTaskRunner::ProcessTask(second_task->dependencies()[0].get());
  TestTileTaskRunner::ProcessTask(second_task.get());

  cache.UnrefImage(second_draw_image);

  // The budget should account for exactly one image.
  EXPECT_EQ(cache.GetBytesUsedForTesting(),
            cache.GetDrawImageSizeForTesting(second_draw_image));
}

// crbug.com/709341.
#if defined(MEMORY_SANITIZER)
#define MAYBE_QualityCappedAtMedium DISABLED_QualityCappedAtMedium
#else
#define MAYBE_QualityCappedAtMedium QualityCappedAtMedium
#endif
TEST(GpuImageDecodeCacheTest, MAYBE_QualityCappedAtMedium) {
  auto context_provider = TestContextProvider::Create();
  context_provider->BindToCurrentThread();
  TestGpuImageDecodeCache cache(context_provider.get());
  sk_sp<SkImage> image = CreateImage(100, 100);
  bool is_decomposable = true;
  SkMatrix matrix = CreateMatrix(SkSize::Make(0.4f, 0.4f), is_decomposable);

  // Create an image with kLow_FilterQuality.
  DrawImage low_draw_image(image,
                           SkIRect::MakeWH(image->width(), image->height()),
                           kLow_SkFilterQuality, matrix, DefaultColorSpace());
  scoped_refptr<TileTask> low_task;
  bool need_unref = cache.GetTaskForImageAndRef(
      low_draw_image, ImageDecodeCache::TracingInfo(), &low_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(low_task);

  // Get the same image at kMedium_FilterQuality. We can't re-use low, so we
  // should get a new task/ref.
  DrawImage medium_draw_image(
      image, SkIRect::MakeWH(image->width(), image->height()),
      kMedium_SkFilterQuality, matrix, DefaultColorSpace());
  scoped_refptr<TileTask> medium_task;
  need_unref = cache.GetTaskForImageAndRef(
      medium_draw_image, ImageDecodeCache::TracingInfo(), &medium_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(medium_task.get());
  EXPECT_FALSE(low_task.get() == medium_task.get());

  // Get the same image at kHigh_FilterQuality. We should re-use medium.
  DrawImage large_draw_image(
      image, SkIRect::MakeWH(image->width(), image->height()),
      kHigh_SkFilterQuality, matrix, DefaultColorSpace());
  scoped_refptr<TileTask> large_task;
  need_unref = cache.GetTaskForImageAndRef(
      large_draw_image, ImageDecodeCache::TracingInfo(), &large_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(medium_task.get() == large_task.get());

  TestTileTaskRunner::ProcessTask(low_task->dependencies()[0].get());
  TestTileTaskRunner::ProcessTask(low_task.get());
  TestTileTaskRunner::ProcessTask(medium_task->dependencies()[0].get());
  TestTileTaskRunner::ProcessTask(medium_task.get());

  cache.UnrefImage(low_draw_image);
  cache.UnrefImage(medium_draw_image);
  cache.UnrefImage(large_draw_image);
}

// Ensure that switching to a mipped version of an image after the initial
// cache entry creation doesn't cause a buffer overflow/crash.
TEST(GpuImageDecodeCacheTest, GetDecodedImageForDrawMipUsageChange) {
  auto context_provider = TestContextProvider::Create();
  context_provider->BindToCurrentThread();
  TestGpuImageDecodeCache cache(context_provider.get());
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  // Create an image decode task and cache entry that does not need mips.
  sk_sp<SkImage> image = CreateImage(4000, 4000);
  DrawImage draw_image(image, SkIRect::MakeWH(image->width(), image->height()),
                       quality,
                       CreateMatrix(SkSize::Make(1.0f, 1.0f), is_decomposable),
                       DefaultColorSpace());
  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(task);

  // Cancel the task without ever using it.
  TestTileTaskRunner::CancelTask(task->dependencies()[0].get());
  TestTileTaskRunner::CompleteTask(task->dependencies()[0].get());
  TestTileTaskRunner::CancelTask(task.get());
  TestTileTaskRunner::CompleteTask(task.get());

  cache.UnrefImage(draw_image);

  // Must hold context lock before calling GetDecodedImageForDraw /
  // DrawWithImageFinished.
  ContextProvider::ScopedContextLock context_lock(context_provider.get());

  // Do an at-raster decode of the above image that *does* require mips.
  DrawImage draw_image_mips(
      image, SkIRect::MakeWH(image->width(), image->height()), quality,
      CreateMatrix(SkSize::Make(0.6f, 0.6f), is_decomposable),
      DefaultColorSpace());
  DecodedDrawImage decoded_draw_image =
      cache.GetDecodedImageForDraw(draw_image_mips);
  cache.DrawWithImageFinished(draw_image_mips, decoded_draw_image);
}

TEST(GpuImageDecodeCacheTest, MemoryStateSuspended) {
  auto context_provider = TestContextProvider::Create();
  context_provider->BindToCurrentThread();
  TestGpuImageDecodeCache cache(context_provider.get());

  // First Insert an image into our cache.
  sk_sp<SkImage> image = CreateImage(1, 1);
  bool is_decomposable = true;
  SkMatrix matrix = CreateMatrix(SkSize::Make(1.0f, 1.0f), is_decomposable);
  DrawImage draw_image(image, SkIRect::MakeWH(image->width(), image->height()),
                       kLow_SkFilterQuality, matrix, DefaultColorSpace());
  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(task);

  TestTileTaskRunner::ProcessTask(task->dependencies()[0].get());
  TestTileTaskRunner::ProcessTask(task.get());
  cache.UnrefImage(draw_image);

  // The image should be cached.
  EXPECT_GT(cache.GetBytesUsedForTesting(), 0u);
  EXPECT_EQ(cache.GetNumCacheEntriesForTesting(), 1u);

  // Set us to the not visible state (prerequisite for SUSPENDED).
  cache.SetShouldAggressivelyFreeResources(true);

  // Image should be cached, but not using memory budget.
  EXPECT_EQ(cache.GetBytesUsedForTesting(), 0u);
  EXPECT_EQ(cache.GetNumCacheEntriesForTesting(), 1u);

  // Set us to the SUSPENDED state with purging.
  cache.OnPurgeMemory();
  cache.OnMemoryStateChange(base::MemoryState::SUSPENDED);

  // Nothing should be cached.
  EXPECT_EQ(cache.GetBytesUsedForTesting(), 0u);
  EXPECT_EQ(cache.GetNumCacheEntriesForTesting(), 0u);

  // Attempts to get a task for the image will still succeed, as SUSPENDED
  // doesn't impact working set size.
  need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(task);

  TestTileTaskRunner::ProcessTask(task->dependencies()[0].get());
  TestTileTaskRunner::ProcessTask(task.get());
  cache.UnrefImage(draw_image);

  // Nothing should be cached.
  EXPECT_EQ(cache.GetBytesUsedForTesting(), 0u);
  EXPECT_EQ(cache.GetNumCacheEntriesForTesting(), 0u);

  // Restore us to visible and NORMAL memory state.
  cache.OnMemoryStateChange(base::MemoryState::NORMAL);
  cache.SetShouldAggressivelyFreeResources(false);

  // We should now be able to create a task again (space available).
  need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(task);

  TestTileTaskRunner::ProcessTask(task->dependencies()[0].get());
  TestTileTaskRunner::ProcessTask(task.get());
  cache.UnrefImage(draw_image);
}

TEST(GpuImageDecodeCacheTest, OutOfRasterDecodeTask) {
  auto context_provider = TestContextProvider::Create();
  context_provider->BindToCurrentThread();
  TestGpuImageDecodeCache cache(context_provider.get());

  sk_sp<SkImage> image = CreateImage(1, 1);
  bool is_decomposable = true;
  SkMatrix matrix = CreateMatrix(SkSize::Make(1.0f, 1.0f), is_decomposable);
  DrawImage draw_image(image, SkIRect::MakeWH(image->width(), image->height()),
                       kLow_SkFilterQuality, matrix, DefaultColorSpace());

  scoped_refptr<TileTask> task;
  bool need_unref =
      cache.GetOutOfRasterDecodeTaskForImageAndRef(draw_image, &task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(task);
  EXPECT_TRUE(cache.IsInInUseCacheForTesting(draw_image));

  // Run the decode task.
  TestTileTaskRunner::ProcessTask(task.get());

  // The image should remain in the cache till we unref it.
  EXPECT_TRUE(cache.IsInInUseCacheForTesting(draw_image));
  cache.UnrefImage(draw_image);
}

TEST(GpuImageDecodeCacheTest, ZeroCacheNormalWorkingSet) {
  // Setup - Image cache has a normal working set, but zero cache size.
  auto context_provider = TestContextProvider::Create();
  context_provider->BindToCurrentThread();
  GpuImageDecodeCache cache(context_provider.get(), ResourceFormat::RGBA_8888,
                            kGpuMemoryLimitBytes, 0);
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  // Add an image to the cache. Due to normal working set, this should produce
  // a task and a ref.
  sk_sp<SkImage> image = CreateImage(100, 100);
  DrawImage draw_image(image, SkIRect::MakeWH(image->width(), image->height()),
                       quality,
                       CreateMatrix(SkSize::Make(1.0f, 1.0f), is_decomposable),
                       DefaultColorSpace());
  scoped_refptr<TileTask> task;
  bool need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(task);
  EXPECT_EQ(task->dependencies().size(), 1u);
  EXPECT_TRUE(task->dependencies()[0]);

  // Run the task.
  TestTileTaskRunner::ProcessTask(task->dependencies()[0].get());
  TestTileTaskRunner::ProcessTask(task.get());

  // Request the same image - it should be cached.
  scoped_refptr<TileTask> task2;
  need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task2);
  EXPECT_TRUE(need_unref);
  EXPECT_FALSE(task2);

  // Unref both images.
  cache.UnrefImage(draw_image);
  cache.UnrefImage(draw_image);

  // Get the image again. As it was fully unreffed, it is no longer in the
  // working set and will be evicted due to 0 cache size.
  scoped_refptr<TileTask> task3;
  need_unref = cache.GetTaskForImageAndRef(
      draw_image, ImageDecodeCache::TracingInfo(), &task3);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(task3);
  EXPECT_EQ(task3->dependencies().size(), 1u);
  EXPECT_TRUE(task->dependencies()[0]);

  TestTileTaskRunner::ProcessTask(task3->dependencies()[0].get());
  TestTileTaskRunner::ProcessTask(task3.get());

  cache.UnrefImage(draw_image);
}

TEST(GpuImageDecodeCacheTest, SmallCacheNormalWorkingSet) {
  // Cache will fit one (but not two) 100x100 images.
  size_t cache_size = 190 * 100 * 4;

  auto context_provider = TestContextProvider::Create();
  context_provider->BindToCurrentThread();
  GpuImageDecodeCache cache(context_provider.get(), ResourceFormat::RGBA_8888,
                            kGpuMemoryLimitBytes, cache_size);
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  sk_sp<SkImage> image = CreateImage(100, 100);
  DrawImage draw_image(image, SkIRect::MakeWH(image->width(), image->height()),
                       quality,
                       CreateMatrix(SkSize::Make(1.0f, 1.0f), is_decomposable),
                       DefaultColorSpace());

  sk_sp<SkImage> image2 = CreateImage(100, 100);
  DrawImage draw_image2(
      image2, SkIRect::MakeWH(image2->width(), image2->height()), quality,
      CreateMatrix(SkSize::Make(1.0f, 1.0f), is_decomposable),
      DefaultColorSpace());

  // Add an image to the cache and un-ref it.
  {
    scoped_refptr<TileTask> task;
    bool need_unref = cache.GetTaskForImageAndRef(
        draw_image, ImageDecodeCache::TracingInfo(), &task);
    EXPECT_TRUE(need_unref);
    EXPECT_TRUE(task);
    EXPECT_EQ(task->dependencies().size(), 1u);
    EXPECT_TRUE(task->dependencies()[0]);

    // Run the task and unref the image.
    TestTileTaskRunner::ProcessTask(task->dependencies()[0].get());
    TestTileTaskRunner::ProcessTask(task.get());
    cache.UnrefImage(draw_image);
  }

  // Request the same image - it should be cached.
  {
    scoped_refptr<TileTask> task;
    bool need_unref = cache.GetTaskForImageAndRef(
        draw_image, ImageDecodeCache::TracingInfo(), &task);
    EXPECT_TRUE(need_unref);
    EXPECT_FALSE(task);
    cache.UnrefImage(draw_image);
  }

  // Add a new image to the cache. It should push out the old one.
  {
    scoped_refptr<TileTask> task;
    bool need_unref = cache.GetTaskForImageAndRef(
        draw_image2, ImageDecodeCache::TracingInfo(), &task);
    EXPECT_TRUE(need_unref);
    EXPECT_TRUE(task);
    EXPECT_EQ(task->dependencies().size(), 1u);
    EXPECT_TRUE(task->dependencies()[0]);

    // Run the task and unref the image.
    TestTileTaskRunner::ProcessTask(task->dependencies()[0].get());
    TestTileTaskRunner::ProcessTask(task.get());
    cache.UnrefImage(draw_image2);
  }

  // Request the second image - it should be cached.
  {
    scoped_refptr<TileTask> task;
    bool need_unref = cache.GetTaskForImageAndRef(
        draw_image2, ImageDecodeCache::TracingInfo(), &task);
    EXPECT_TRUE(need_unref);
    EXPECT_FALSE(task);
    cache.UnrefImage(draw_image2);
  }

  // Request the first image - it should have been evicted and return a new
  // task.
  {
    scoped_refptr<TileTask> task;
    bool need_unref = cache.GetTaskForImageAndRef(
        draw_image, ImageDecodeCache::TracingInfo(), &task);
    EXPECT_TRUE(need_unref);
    EXPECT_TRUE(task);
    EXPECT_EQ(task->dependencies().size(), 1u);
    EXPECT_TRUE(task->dependencies()[0]);

    // Run the task and unref the image.
    TestTileTaskRunner::ProcessTask(task->dependencies()[0].get());
    TestTileTaskRunner::ProcessTask(task.get());
    cache.UnrefImage(draw_image);
  }
}

TEST(GpuImageDecodeCacheTest, ClearCache) {
  auto context_provider = TestContextProvider::Create();
  context_provider->BindToCurrentThread();
  TestGpuImageDecodeCache cache(context_provider.get());
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  for (int i = 0; i < 10; ++i) {
    sk_sp<SkImage> image = CreateImage(100, 100);
    DrawImage draw_image(
        image, SkIRect::MakeWH(image->width(), image->height()), quality,
        CreateMatrix(SkSize::Make(1.0f, 1.0f), is_decomposable),
        DefaultColorSpace());
    scoped_refptr<TileTask> task;
    bool need_unref = cache.GetTaskForImageAndRef(
        draw_image, ImageDecodeCache::TracingInfo(), &task);
    EXPECT_TRUE(need_unref);
    EXPECT_TRUE(task);
    TestTileTaskRunner::ProcessTask(task->dependencies()[0].get());
    TestTileTaskRunner::ProcessTask(task.get());
    cache.UnrefImage(draw_image);
  }

  // We should now have data image in our cache.
  EXPECT_GT(cache.GetBytesUsedForTesting(), 0u);
  EXPECT_EQ(cache.GetNumCacheEntriesForTesting(), 10u);

  // Tell our cache to clear resources.
  cache.ClearCache();

  // We should now have nothing in our cache.
  EXPECT_EQ(cache.GetBytesUsedForTesting(), 0u);
  EXPECT_EQ(cache.GetNumCacheEntriesForTesting(), 0u);
}

TEST(GpuImageDecodeCacheTest, GetTaskForImageDifferentColorSpace) {
  auto context_provider = TestContextProvider::Create();
  context_provider->BindToCurrentThread();
  TestGpuImageDecodeCache cache(context_provider.get());
  bool is_decomposable = true;
  SkFilterQuality quality = kHigh_SkFilterQuality;

  gfx::ColorSpace color_space_a = gfx::ColorSpace::CreateSRGB();
  gfx::ColorSpace color_space_b = gfx::ColorSpace::CreateXYZD50();

  sk_sp<SkImage> first_image = CreateImage(100, 100);
  DrawImage first_draw_image(
      first_image, SkIRect::MakeWH(first_image->width(), first_image->height()),
      quality, CreateMatrix(SkSize::Make(1.0f, 1.0f), is_decomposable),
      color_space_a);
  scoped_refptr<TileTask> first_task;
  bool need_unref = cache.GetTaskForImageAndRef(
      first_draw_image, ImageDecodeCache::TracingInfo(), &first_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(first_task);

  DrawImage second_draw_image(
      first_image, SkIRect::MakeWH(first_image->width(), first_image->height()),
      quality, CreateMatrix(SkSize::Make(1.0f, 1.0f), is_decomposable),
      color_space_b);
  scoped_refptr<TileTask> second_task;
  need_unref = cache.GetTaskForImageAndRef(
      second_draw_image, ImageDecodeCache::TracingInfo(), &second_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(second_task);
  EXPECT_TRUE(first_task.get() != second_task.get());

  DrawImage third_draw_image(
      first_image, SkIRect::MakeWH(first_image->width(), first_image->height()),
      quality, CreateMatrix(SkSize::Make(1.0f, 1.0f), is_decomposable),
      color_space_a);
  scoped_refptr<TileTask> third_task;
  need_unref = cache.GetTaskForImageAndRef(
      third_draw_image, ImageDecodeCache::TracingInfo(), &third_task);
  EXPECT_TRUE(need_unref);
  EXPECT_TRUE(third_task.get() == first_task.get());

  TestTileTaskRunner::ProcessTask(first_task->dependencies()[0].get());
  TestTileTaskRunner::ProcessTask(first_task.get());
  TestTileTaskRunner::ProcessTask(second_task->dependencies()[0].get());
  TestTileTaskRunner::ProcessTask(second_task.get());

  cache.UnrefImage(first_draw_image);
  cache.UnrefImage(second_draw_image);
  cache.UnrefImage(third_draw_image);
}

}  // namespace
}  // namespace cc
