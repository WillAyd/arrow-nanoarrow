// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include <cerrno>
#include <cstring>
#include <string>

#include <gtest/gtest.h>

#include "nanoarrow/nanoarrow.h"

// This test allocator guarantees that allocator->reallocate will return
// a new pointer so that we can test when reallocations happen whilst
// building buffers.
static uint8_t* TestAllocatorReallocate(struct ArrowBufferAllocator* allocator,
                                        uint8_t* ptr, int64_t old_size,
                                        int64_t new_size) {
  uint8_t* new_ptr = reinterpret_cast<uint8_t*>(malloc(new_size));

  int64_t copy_size = std::min<int64_t>(old_size, new_size);
  if (new_ptr != nullptr && copy_size > 0) {
    memcpy(new_ptr, ptr, copy_size);
  }

  if (ptr != nullptr) {
    free(ptr);
  }

  return new_ptr;
}

static void TestAllocatorFree(struct ArrowBufferAllocator* allocator, uint8_t* ptr,
                              int64_t size) {
  free(ptr);
}

static struct ArrowBufferAllocator test_allocator = {&TestAllocatorReallocate,
                                                     &TestAllocatorFree, nullptr};

TEST(BufferTest, BufferTestBasic) {
  struct ArrowBuffer buffer;

  // Init
  ArrowBufferInit(&buffer);
  ASSERT_EQ(ArrowBufferSetAllocator(&buffer, test_allocator), NANOARROW_OK);
  EXPECT_EQ(buffer.data, nullptr);
  EXPECT_EQ(buffer.capacity_bytes, 0);
  EXPECT_EQ(buffer.size_bytes, 0);

  // Reserve where capacity > current_capacity * growth_factor
  EXPECT_EQ(ArrowBufferReserve(&buffer, 10), NANOARROW_OK);
  EXPECT_NE(buffer.data, nullptr);
  EXPECT_EQ(buffer.capacity_bytes, 10);
  EXPECT_EQ(buffer.size_bytes, 0);

  // Write without triggering a realloc
  uint8_t* first_data = buffer.data;
  EXPECT_EQ(ArrowBufferAppend(&buffer, "1234567890", 10), NANOARROW_OK);
  EXPECT_EQ(buffer.data, first_data);
  EXPECT_EQ(buffer.capacity_bytes, 10);
  EXPECT_EQ(buffer.size_bytes, 10);

  // Write triggering a realloc
  EXPECT_EQ(ArrowBufferAppend(&buffer, "1", 2), NANOARROW_OK);
  EXPECT_NE(buffer.data, first_data);
  EXPECT_EQ(buffer.capacity_bytes, 20);
  EXPECT_EQ(buffer.size_bytes, 12);
  EXPECT_STREQ(reinterpret_cast<char*>(buffer.data), "12345678901");

  // Resize smaller without shrinking
  EXPECT_EQ(ArrowBufferResize(&buffer, 5, false), NANOARROW_OK);
  EXPECT_EQ(buffer.capacity_bytes, 20);
  EXPECT_EQ(buffer.size_bytes, 5);
  EXPECT_EQ(strncmp(reinterpret_cast<char*>(buffer.data), "12345", 5), 0);

  // Resize smaller with shrinking
  EXPECT_EQ(ArrowBufferResize(&buffer, 4, true), NANOARROW_OK);
  EXPECT_EQ(buffer.capacity_bytes, 4);
  EXPECT_EQ(buffer.size_bytes, 4);
  EXPECT_EQ(strncmp(reinterpret_cast<char*>(buffer.data), "1234", 4), 0);

  // Reset the buffer
  ArrowBufferReset(&buffer);
  EXPECT_EQ(buffer.data, nullptr);
  EXPECT_EQ(buffer.capacity_bytes, 0);
  EXPECT_EQ(buffer.size_bytes, 0);
}

TEST(BufferTest, BufferTestMove) {
  struct ArrowBuffer buffer;

  ArrowBufferInit(&buffer);
  ASSERT_EQ(ArrowBufferSetAllocator(&buffer, test_allocator), NANOARROW_OK);
  ASSERT_EQ(ArrowBufferAppend(&buffer, "1234567", 7), NANOARROW_OK);
  EXPECT_EQ(buffer.size_bytes, 7);
  EXPECT_EQ(buffer.capacity_bytes, 7);

  struct ArrowBuffer buffer_out;
  ArrowBufferMove(&buffer, &buffer_out);
  EXPECT_EQ(buffer.size_bytes, 0);
  EXPECT_EQ(buffer.capacity_bytes, 0);
  EXPECT_EQ(buffer.data, nullptr);
  EXPECT_EQ(buffer_out.size_bytes, 7);
  EXPECT_EQ(buffer_out.capacity_bytes, 7);

  ArrowBufferReset(&buffer_out);
}

TEST(BufferTest, BufferTestFill) {
  struct ArrowBuffer buffer;
  ArrowBufferInit(&buffer);

  EXPECT_EQ(ArrowBufferAppendFill(&buffer, 0xff, 10), NANOARROW_OK);
  EXPECT_EQ(buffer.size_bytes, 10);
  for (int i = 0; i < 10; i++) {
    EXPECT_EQ(buffer.data[i], 0xff);
  }

  buffer.size_bytes = 0;
  EXPECT_EQ(ArrowBufferAppendFill(&buffer, 0, 10), NANOARROW_OK);
  EXPECT_EQ(buffer.size_bytes, 10);
  for (int i = 0; i < 10; i++) {
    EXPECT_EQ(buffer.data[i], 0);
  }

  ArrowBufferReset(&buffer);

  EXPECT_EQ(ArrowBufferAppendFill(&buffer, 0, std::numeric_limits<int64_t>::max()),
            ENOMEM);
}

TEST(BufferTest, BufferTestResize0) {
  struct ArrowBuffer buffer;

  ArrowBufferInit(&buffer);
  ASSERT_EQ(ArrowBufferSetAllocator(&buffer, test_allocator), NANOARROW_OK);
  ASSERT_EQ(ArrowBufferAppend(&buffer, "1234567", 7), NANOARROW_OK);
  EXPECT_EQ(buffer.size_bytes, 7);
  EXPECT_EQ(buffer.capacity_bytes, 7);

  EXPECT_EQ(ArrowBufferResize(&buffer, 0, false), NANOARROW_OK);
  EXPECT_EQ(buffer.size_bytes, 0);
  EXPECT_EQ(buffer.capacity_bytes, 7);

  EXPECT_EQ(ArrowBufferResize(&buffer, 0, true), NANOARROW_OK);
  EXPECT_EQ(buffer.size_bytes, 0);
  EXPECT_EQ(buffer.capacity_bytes, 0);

  ArrowBufferReset(&buffer);
}

TEST(BufferTest, BufferTestError) {
  struct ArrowBuffer buffer;
  ArrowBufferInit(&buffer);
  EXPECT_EQ(ArrowBufferResize(&buffer, std::numeric_limits<int64_t>::max(), false),
            ENOMEM);
  EXPECT_EQ(ArrowBufferAppend(&buffer, nullptr, std::numeric_limits<int64_t>::max()),
            ENOMEM);

  ASSERT_EQ(ArrowBufferAppend(&buffer, "abcd", 4), NANOARROW_OK);
  EXPECT_EQ(ArrowBufferSetAllocator(&buffer, ArrowBufferAllocatorDefault()), EINVAL);

  EXPECT_EQ(ArrowBufferResize(&buffer, -1, false), EINVAL);

  ArrowBufferReset(&buffer);
}

TEST(BufferTest, BufferTestAppendHelpers) {
  struct ArrowBuffer buffer;
  ArrowBufferInit(&buffer);

  EXPECT_EQ(ArrowBufferAppendInt8(&buffer, 123), NANOARROW_OK);
  EXPECT_EQ(reinterpret_cast<int8_t*>(buffer.data)[0], 123);
  ArrowBufferReset(&buffer);

  EXPECT_EQ(ArrowBufferAppendUInt8(&buffer, 123), NANOARROW_OK);
  EXPECT_EQ(reinterpret_cast<uint8_t*>(buffer.data)[0], 123);
  ArrowBufferReset(&buffer);

  EXPECT_EQ(ArrowBufferAppendInt16(&buffer, 123), NANOARROW_OK);
  EXPECT_EQ(reinterpret_cast<int16_t*>(buffer.data)[0], 123);
  ArrowBufferReset(&buffer);

  EXPECT_EQ(ArrowBufferAppendUInt16(&buffer, 123), NANOARROW_OK);
  EXPECT_EQ(reinterpret_cast<uint16_t*>(buffer.data)[0], 123);
  ArrowBufferReset(&buffer);

  EXPECT_EQ(ArrowBufferAppendInt32(&buffer, 123), NANOARROW_OK);
  EXPECT_EQ(reinterpret_cast<int32_t*>(buffer.data)[0], 123);
  ArrowBufferReset(&buffer);

  EXPECT_EQ(ArrowBufferAppendUInt32(&buffer, 123), NANOARROW_OK);
  EXPECT_EQ(reinterpret_cast<uint32_t*>(buffer.data)[0], 123);
  ArrowBufferReset(&buffer);

  EXPECT_EQ(ArrowBufferAppendInt64(&buffer, 123), NANOARROW_OK);
  EXPECT_EQ(reinterpret_cast<int64_t*>(buffer.data)[0], 123);
  ArrowBufferReset(&buffer);

  EXPECT_EQ(ArrowBufferAppendUInt64(&buffer, 123), NANOARROW_OK);
  EXPECT_EQ(reinterpret_cast<uint64_t*>(buffer.data)[0], 123);
  ArrowBufferReset(&buffer);

  EXPECT_EQ(ArrowBufferAppendDouble(&buffer, 123), NANOARROW_OK);
  EXPECT_EQ(reinterpret_cast<double*>(buffer.data)[0], 123);
  ArrowBufferReset(&buffer);

  EXPECT_EQ(ArrowBufferAppendFloat(&buffer, 123), NANOARROW_OK);
  EXPECT_EQ(reinterpret_cast<float*>(buffer.data)[0], 123);
  ArrowBufferReset(&buffer);
}
