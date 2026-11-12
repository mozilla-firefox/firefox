/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <vector>

#include "gfxPlatform.h"
#include "gtest/gtest.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/Blur.h"
#include "mozilla/gfx/DrawEventRecorder.h"
#include "mozilla/gfx/Filters.h"
#include "mozilla/gfx/InlineTranslator.h"

using namespace mozilla;
using namespace mozilla::gfx;

static const IntSize kSize(64, 64);

static already_AddRefed<DataSourceSurface> MakeCheckerboard() {
  RefPtr<DataSourceSurface> surface = Factory::CreateDataSourceSurface(
      IntSize(8, 8), SurfaceFormat::B8G8R8A8, true);
  DataSourceSurface::ScopedMap map(surface, DataSourceSurface::WRITE);
  for (int32_t y = 0; y < 8; y++) {
    uint32_t* row =
        reinterpret_cast<uint32_t*>(map.GetData() + y * map.GetStride());
    for (int32_t x = 0; x < 8; x++) {
      row[x] = (x + y) % 2 ? 0xff00ff00 : 0x80000080;
    }
  }
  return surface.forget();
}

// Draws content that references gradient stops, surfaces and filter nodes, so
// that the recording contains object references.
static void DrawContent(DrawTarget* aDT) {
  GradientStop stops[2] = {{0.0f, DeviceColor(1.0f, 0.0f, 0.0f, 1.0f)},
                           {1.0f, DeviceColor(0.0f, 0.0f, 1.0f, 0.5f)}};
  RefPtr<GradientStops> gradientStops =
      aDT->CreateGradientStops(stops, 2, ExtendMode::CLAMP);
  aDT->FillRect(
      Rect(0, 0, 32, 32),
      LinearGradientPattern(Point(0, 0), Point(32, 32), gradientStops));
  aDT->FillRect(Rect(32, 0, 32, 32),
                RadialGradientPattern(Point(48, 16), Point(48, 16), 0.0f, 16.0f,
                                      gradientStops));

  RefPtr<DataSourceSurface> surface = MakeCheckerboard();
  aDT->FillRect(Rect(0, 32, 32, 32),
                SurfacePattern(surface, ExtendMode::REPEAT));

  RefPtr<FilterNode> blur = aDT->CreateFilter(FilterType::GAUSSIAN_BLUR);
  blur->SetAttribute(ATT_GAUSSIAN_BLUR_STD_DEVIATION, 3.0f);
  blur->SetInput(IN_GAUSSIAN_BLUR_IN, surface);
  aDT->DrawFilter(blur, Rect(0, 0, 8, 8), Point(44, 44));
}

static std::vector<char> Record(bool aDeterministic,
                                const Matrix& aTransform = Matrix(),
                                Float* aOutMaxBlur = nullptr) {
  RefPtr<DrawTarget> referenceDT = Factory::CreateDrawTarget(
      BackendType::SKIA, IntSize(1, 1), SurfaceFormat::B8G8R8A8);
  RefPtr<DrawEventRecorderMemory> recorder =
      MakeAndAddRef<DrawEventRecorderMemory>();
  if (aDeterministic) {
    recorder->UseDeterministicReferenceIds();
  }
  {
    RefPtr<DrawTarget> dt = Factory::CreateRecordingDrawTarget(
        recorder, referenceDT, IntRect(IntPoint(), kSize));
    dt->SetTransform(aTransform);
    DrawContent(dt);
  }
  if (aOutMaxBlur) {
    *aOutMaxBlur = recorder->MaxBlurStdDeviation();
  }
  return std::vector<char>(
      recorder->mOutputStream.mData,
      recorder->mOutputStream.mData + recorder->mOutputStream.mLength);
}

static std::vector<uint8_t> Replay(std::vector<char>& aRecording) {
  RefPtr<DrawTarget> dt = Factory::CreateDrawTarget(BackendType::SKIA, kSize,
                                                    SurfaceFormat::B8G8R8A8);
  InlineTranslator translator(dt);
  EXPECT_TRUE(
      translator.TranslateRecording(aRecording.data(), aRecording.size()));
  RefPtr<SourceSurface> snapshot = dt->Snapshot();
  RefPtr<DataSourceSurface> data = snapshot->GetDataSurface();
  DataSourceSurface::ScopedMap map(data, DataSourceSurface::READ);
  std::vector<uint8_t> pixels;
  for (int32_t y = 0; y < kSize.height; y++) {
    const uint8_t* row = map.GetData() + y * map.GetStride();
    pixels.insert(pixels.end(), row, row + kSize.width * 4);
  }
  return pixels;
}

TEST(Moz2D, DeterministicRecordingIsReproducible)
{
  gfxPlatform::GetPlatform();
  std::vector<char> first = Record(true);
  std::vector<char> second = Record(true);
  EXPECT_FALSE(first.empty());
  EXPECT_EQ(first, second);
}

TEST(Moz2D, DeterministicRecordingReplaysLikeRegularRecording)
{
  gfxPlatform::GetPlatform();
  std::vector<char> deterministic = Record(true);
  std::vector<char> regular = Record(false);
  std::vector<uint8_t> expected = Replay(regular);
  std::vector<uint8_t> actual = Replay(deterministic);
  bool drewSomething = false;
  for (uint8_t value : expected) {
    drewSomething |= value != 0;
  }
  EXPECT_TRUE(drewSomething);
  EXPECT_EQ(expected, actual);
}

TEST(Moz2D, RecordingTracksMaxBlurStdDeviation)
{
  gfxPlatform::GetPlatform();
  Float maxBlur = 0.0f;
  Record(false, Matrix(), &maxBlur);
  EXPECT_FLOAT_EQ(maxBlur, 3.0f);
  Record(false, Matrix::Scaling(2.0f, 0.5f), &maxBlur);
  EXPECT_FLOAT_EQ(maxBlur, 6.0f);
}

TEST(Moz2D, RecordingClampsBlurStdDeviationLikeSoftwareFilters)
{
  gfxPlatform::GetPlatform();
  RefPtr<DrawTarget> referenceDT = Factory::CreateDrawTarget(
      BackendType::SKIA, IntSize(1, 1), SurfaceFormat::B8G8R8A8);
  RefPtr<DrawEventRecorderMemory> recorder =
      MakeAndAddRef<DrawEventRecorderMemory>();
  RefPtr<DrawTarget> dt = Factory::CreateRecordingDrawTarget(
      recorder, referenceDT, IntRect(IntPoint(), kSize));
  dt->SetTransform(Matrix::Scaling(2.0f, 2.0f));
  RefPtr<DataSourceSurface> surface = MakeCheckerboard();
  RefPtr<FilterNode> blur = dt->CreateFilter(FilterType::GAUSSIAN_BLUR);
  blur->SetAttribute(ATT_GAUSSIAN_BLUR_STD_DEVIATION, 250.0f);
  blur->SetInput(IN_GAUSSIAN_BLUR_IN, surface);
  dt->DrawFilter(blur, Rect(0, 0, 8, 8), Point());
  EXPECT_FLOAT_EQ(recorder->MaxBlurStdDeviation(),
                  2.0f * GaussianBlur::kMaxSoftwareStdDeviation);
}

TEST(Moz2D, RecordingWritesZeroedPadding)
{
  static_assert(sizeof(DrawOptions) >
                sizeof(Float) + sizeof(CompositionOp) + sizeof(AntialiasMode));
  alignas(DrawOptions) unsigned char dirty[sizeof(DrawOptions)];
  alignas(DrawOptions) unsigned char clean[sizeof(DrawOptions)];
  memset(dirty, 0xAB, sizeof(dirty));
  memset(clean, 0, sizeof(clean));
  new (dirty) DrawOptions(0.5f, CompositionOp::OP_SOURCE, AntialiasMode::NONE);
  new (clean) DrawOptions(0.5f, CompositionOp::OP_SOURCE, AntialiasMode::NONE);

  MemStream dirtyStream;
  MemStream cleanStream;
  WriteElement(dirtyStream, *reinterpret_cast<DrawOptions*>(dirty));
  WriteElement(cleanStream, *reinterpret_cast<DrawOptions*>(clean));
  ASSERT_EQ(dirtyStream.mLength, cleanStream.mLength);
  EXPECT_EQ(memcmp(dirtyStream.mData, cleanStream.mData, dirtyStream.mLength),
            0);
}
