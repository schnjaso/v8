// Copyright 2016 the V8 project authors. All rights reserved.
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
//     * Redistributions of source code must retain the above copyright
//       notice, this list of conditions and the following disclaimer.
//     * Redistributions in binary form must reproduce the above
//       copyright notice, this list of conditions and the following
//       disclaimer in the documentation and/or other materials provided
//       with the distribution.
//     * Neither the name of Google Inc. nor the names of its
//       contributors may be used to endorse or promote products derived
//       from this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "src/v8.h"
#include "test/cctest/cctest.h"
#include "test/cctest/compiler/value-helper.h"

#include "src/arm/simulator-arm.h"
#include "src/assembler-inl.h"
#include "src/disassembler.h"
#include "src/factory.h"
#include "src/macro-assembler.h"

namespace v8 {
namespace internal {

#ifndef V8_TARGET_LITTLE_ENDIAN
#error Expected ARM to be little-endian
#endif

// Define these function prototypes to match JSEntryFunction in execution.cc.
typedef Object* (*F_iiiii)(int p0, int p1, int p2, int p3, int p4);
typedef Object* (*F_piiii)(void* p0, int p1, int p2, int p3, int p4);

#define __ assm.

namespace {

struct MemoryAccess {
  enum class Kind {
    None,
    Load,
    LoadExcl,
    Store,
    StoreExcl,
  };

  enum class Size {
    Byte,
    HalfWord,
    Word,
  };

  MemoryAccess() : kind(Kind::None) {}
  MemoryAccess(Kind kind, Size size, size_t offset, int value = 0)
      : kind(kind), size(size), offset(offset), value(value) {}

  Kind kind = Kind::None;
  Size size = Size::Byte;
  size_t offset = 0;
  int value = 0;
};

struct TestData {
  explicit TestData(int w) : w(w) {}

  union {
    int32_t w;
    int16_t h;
    int8_t b;
  };
  int dummy;
};

void AssembleMemoryAccess(Assembler* assembler, MemoryAccess access,
                          Register dest_reg, Register value_reg,
                          Register addr_reg) {
  Assembler& assm = *assembler;
  __ add(addr_reg, r0, Operand(access.offset));

  switch (access.kind) {
    case MemoryAccess::Kind::None:
      break;

    case MemoryAccess::Kind::Load:
      switch (access.size) {
        case MemoryAccess::Size::Byte:
          __ ldrb(value_reg, MemOperand(addr_reg));
          break;

        case MemoryAccess::Size::HalfWord:
          __ ldrh(value_reg, MemOperand(addr_reg));
          break;

        case MemoryAccess::Size::Word:
          __ ldr(value_reg, MemOperand(addr_reg));
          break;
      }
      break;

    case MemoryAccess::Kind::LoadExcl:
      switch (access.size) {
        case MemoryAccess::Size::Byte:
          __ ldrexb(value_reg, addr_reg);
          break;

        case MemoryAccess::Size::HalfWord:
          __ ldrexh(value_reg, addr_reg);
          break;

        case MemoryAccess::Size::Word:
          __ ldrex(value_reg, addr_reg);
          break;
      }
      break;

    case MemoryAccess::Kind::Store:
      switch (access.size) {
        case MemoryAccess::Size::Byte:
          __ mov(value_reg, Operand(access.value));
          __ strb(value_reg, MemOperand(addr_reg));
          break;

        case MemoryAccess::Size::HalfWord:
          __ mov(value_reg, Operand(access.value));
          __ strh(value_reg, MemOperand(addr_reg));
          break;

        case MemoryAccess::Size::Word:
          __ mov(value_reg, Operand(access.value));
          __ str(value_reg, MemOperand(addr_reg));
          break;
      }
      break;

    case MemoryAccess::Kind::StoreExcl:
      switch (access.size) {
        case MemoryAccess::Size::Byte:
          __ mov(value_reg, Operand(access.value));
          __ strexb(dest_reg, value_reg, addr_reg);
          break;

        case MemoryAccess::Size::HalfWord:
          __ mov(value_reg, Operand(access.value));
          __ strexh(dest_reg, value_reg, addr_reg);
          break;

        case MemoryAccess::Size::Word:
          __ mov(value_reg, Operand(access.value));
          __ strex(dest_reg, value_reg, addr_reg);
          break;
      }
      break;
  }
}

Address AssembleCode(std::function<void(Assembler&)> assemble) {
  Isolate* isolate = CcTest::i_isolate();
  Assembler assm(isolate, nullptr, 0);

  assemble(assm);

  __ bx(lr);

  CodeDesc desc;
  assm.GetCode(isolate, &desc);
  Handle<Code> code =
      isolate->factory()->NewCode(desc, Code::STUB, Handle<Code>());
  return code->entry();
}

#if defined(USE_SIMULATOR)
void AssembleLoadExcl(Assembler* assembler, MemoryAccess access,
                      Register value_reg, Register addr_reg) {
  DCHECK(access.kind == MemoryAccess::Kind::LoadExcl);
  AssembleMemoryAccess(assembler, access, no_reg, value_reg, addr_reg);
}

void AssembleStoreExcl(Assembler* assembler, MemoryAccess access,
                       Register dest_reg, Register value_reg,
                       Register addr_reg) {
  DCHECK(access.kind == MemoryAccess::Kind::StoreExcl);
  AssembleMemoryAccess(assembler, access, dest_reg, value_reg, addr_reg);
}

void TestInvalidateExclusiveAccess(TestData initial_data, MemoryAccess access1,
                                   MemoryAccess access2, MemoryAccess access3,
                                   int expected_res, TestData expected_data) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  F_piiii f = FUNCTION_CAST<F_piiii>(AssembleCode([&](Assembler& assm) {
    AssembleLoadExcl(&assm, access1, r1, r1);
    AssembleMemoryAccess(&assm, access2, r3, r2, r1);
    AssembleStoreExcl(&assm, access3, r0, r3, r1);
  }));

  TestData t = initial_data;

  int res =
      reinterpret_cast<int>(CALL_GENERATED_CODE(isolate, f, &t, 0, 0, 0, 0));
  CHECK_EQ(expected_res, res);
  switch (access3.size) {
    case MemoryAccess::Size::Byte:
      CHECK_EQ(expected_data.b, t.b);
      break;

    case MemoryAccess::Size::HalfWord:
      CHECK_EQ(expected_data.h, t.h);
      break;

    case MemoryAccess::Size::Word:
      CHECK_EQ(expected_data.w, t.w);
      break;
  }
}
#endif

std::vector<Float32> Float32Inputs() {
  std::vector<Float32> inputs;
  FOR_FLOAT32_INPUTS(f) {
    inputs.push_back(Float32::FromBits(bit_cast<uint32_t>(*f)));
  }
  FOR_UINT32_INPUTS(bits) { inputs.push_back(Float32::FromBits(*bits)); }
  return inputs;
}

std::vector<Float64> Float64Inputs() {
  std::vector<Float64> inputs;
  FOR_FLOAT64_INPUTS(f) {
    inputs.push_back(Float64::FromBits(bit_cast<uint64_t>(*f)));
  }
  FOR_UINT64_INPUTS(bits) { inputs.push_back(Float64::FromBits(*bits)); }
  return inputs;
}

}  // namespace

// TODO(rodolph.perfetta@arm.com): Enable this test for native hardware, see
// http://crbug.com/v8/6963.
#if defined(USE_SIMULATOR)

TEST(simulator_invalidate_exclusive_access) {
  using Kind = MemoryAccess::Kind;
  using Size = MemoryAccess::Size;

  MemoryAccess ldrex_w(Kind::LoadExcl, Size::Word, offsetof(TestData, w));
  MemoryAccess strex_w(Kind::StoreExcl, Size::Word, offsetof(TestData, w), 7);

  // Address mismatch.
  TestInvalidateExclusiveAccess(
      TestData(1), ldrex_w,
      MemoryAccess(Kind::LoadExcl, Size::Word, offsetof(TestData, dummy)),
      strex_w, 1, TestData(1));

  // Size mismatch.
  TestInvalidateExclusiveAccess(
      TestData(1), ldrex_w, MemoryAccess(),
      MemoryAccess(Kind::StoreExcl, Size::HalfWord, offsetof(TestData, w), 7),
      1, TestData(1));

  // Load between ldrex/strex.
  TestInvalidateExclusiveAccess(
      TestData(1), ldrex_w,
      MemoryAccess(Kind::Load, Size::Word, offsetof(TestData, dummy)), strex_w,
      1, TestData(1));

  // Store between ldrex/strex.
  TestInvalidateExclusiveAccess(
      TestData(1), ldrex_w,
      MemoryAccess(Kind::Store, Size::Word, offsetof(TestData, dummy)), strex_w,
      1, TestData(1));

  // Match
  TestInvalidateExclusiveAccess(TestData(1), ldrex_w, MemoryAccess(), strex_w,
                                0, TestData(7));
}

#endif  // USE_SIMULATOR

static int ExecuteMemoryAccess(Isolate* isolate, TestData* test_data,
                               MemoryAccess access) {
  HandleScope scope(isolate);
  F_piiii f = FUNCTION_CAST<F_piiii>(AssembleCode([&](Assembler& assm) {
    AssembleMemoryAccess(&assm, access, r0, r2, r1);
  }));

  return reinterpret_cast<int>(
      CALL_GENERATED_CODE(isolate, f, test_data, 0, 0, 0, 0));
}

class MemoryAccessThread : public v8::base::Thread {
 public:
  MemoryAccessThread()
      : Thread(Options("MemoryAccessThread")),
        test_data_(nullptr),
        is_finished_(false),
        has_request_(false),
        did_request_(false),
        isolate_(nullptr) {}

  virtual void Run() {
    v8::Isolate::CreateParams create_params;
    create_params.array_buffer_allocator = CcTest::array_buffer_allocator();
    isolate_ = v8::Isolate::New(create_params);
    Isolate* i_isolate = reinterpret_cast<Isolate*>(isolate_);
    {
      v8::Isolate::Scope scope(isolate_);
      v8::base::LockGuard<v8::base::Mutex> lock_guard(&mutex_);
      while (!is_finished_) {
        while (!(has_request_ || is_finished_)) {
          has_request_cv_.Wait(&mutex_);
        }

        if (is_finished_) {
          break;
        }

        ExecuteMemoryAccess(i_isolate, test_data_, access_);
        has_request_ = false;
        did_request_ = true;
        did_request_cv_.NotifyOne();
      }
    }
    isolate_->Dispose();
  }

  void NextAndWait(TestData* test_data, MemoryAccess access) {
    DCHECK(!has_request_);
    v8::base::LockGuard<v8::base::Mutex> lock_guard(&mutex_);
    test_data_ = test_data;
    access_ = access;
    has_request_ = true;
    has_request_cv_.NotifyOne();
    while (!did_request_) {
      did_request_cv_.Wait(&mutex_);
    }
    did_request_ = false;
  }

  void Finish() {
    v8::base::LockGuard<v8::base::Mutex> lock_guard(&mutex_);
    is_finished_ = true;
    has_request_cv_.NotifyOne();
  }

 private:
  TestData* test_data_;
  MemoryAccess access_;
  bool is_finished_;
  bool has_request_;
  bool did_request_;
  v8::base::Mutex mutex_;
  v8::base::ConditionVariable has_request_cv_;
  v8::base::ConditionVariable did_request_cv_;
  v8::Isolate* isolate_;
};

// TODO(rodolph.perfetta@arm.com): Enable this test for native hardware, see
// http://crbug.com/v8/6963.
#if defined(USE_SIMULATOR)

TEST(simulator_invalidate_exclusive_access_threaded) {
  using Kind = MemoryAccess::Kind;
  using Size = MemoryAccess::Size;

  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  TestData test_data(1);

  MemoryAccessThread thread;
  thread.Start();

  MemoryAccess ldrex_w(Kind::LoadExcl, Size::Word, offsetof(TestData, w));
  MemoryAccess strex_w(Kind::StoreExcl, Size::Word, offsetof(TestData, w), 7);

  // Exclusive store completed by another thread first.
  test_data = TestData(1);
  thread.NextAndWait(&test_data, MemoryAccess(Kind::LoadExcl, Size::Word,
                                              offsetof(TestData, w)));
  ExecuteMemoryAccess(isolate, &test_data, ldrex_w);
  thread.NextAndWait(&test_data, MemoryAccess(Kind::StoreExcl, Size::Word,
                                              offsetof(TestData, w), 5));
  CHECK_EQ(1, ExecuteMemoryAccess(isolate, &test_data, strex_w));
  CHECK_EQ(5, test_data.w);

  // Exclusive store completed by another thread; different address, but masked
  // to same
  test_data = TestData(1);
  ExecuteMemoryAccess(isolate, &test_data, ldrex_w);
  thread.NextAndWait(&test_data, MemoryAccess(Kind::LoadExcl, Size::Word,
                                              offsetof(TestData, dummy)));
  thread.NextAndWait(&test_data, MemoryAccess(Kind::StoreExcl, Size::Word,
                                              offsetof(TestData, dummy), 5));
  CHECK_EQ(1, ExecuteMemoryAccess(isolate, &test_data, strex_w));
  CHECK_EQ(1, test_data.w);

  // Test failure when store between ldrex/strex.
  test_data = TestData(1);
  ExecuteMemoryAccess(isolate, &test_data, ldrex_w);
  thread.NextAndWait(&test_data, MemoryAccess(Kind::Store, Size::Word,
                                              offsetof(TestData, dummy)));
  CHECK_EQ(1, ExecuteMemoryAccess(isolate, &test_data, strex_w));
  CHECK_EQ(1, test_data.w);

  thread.Finish();
  thread.Join();
}

#endif  // USE_SIMULATOR

TEST(simulator_vabs_32) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  F_iiiii f = FUNCTION_CAST<F_iiiii>(AssembleCode([](Assembler& assm) {
    __ vmov(s0, r0);
    __ vabs(s0, s0);
    __ vmov(r0, s0);
  }));

  for (Float32 f32 : Float32Inputs()) {
    Float32 res = Float32::FromBits(reinterpret_cast<uint32_t>(
        CALL_GENERATED_CODE(isolate, f, f32.get_bits(), 0, 0, 0, 0)));
    Float32 exp = Float32::FromBits(f32.get_bits() & ~(1 << 31));
    CHECK_EQ(exp.get_bits(), res.get_bits());
  }
}

TEST(simulator_vabs_64) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  F_iiiii f = FUNCTION_CAST<F_iiiii>(AssembleCode([](Assembler& assm) {
    __ vmov(d0, r0, r1);
    __ vabs(d0, d0);
    __ vmov(r1, r0, d0);
  }));

  for (Float64 f64 : Float64Inputs()) {
    uint32_t p0 = static_cast<uint32_t>(f64.get_bits());
    uint32_t p1 = static_cast<uint32_t>(f64.get_bits() >> 32);
    uint32_t res = reinterpret_cast<uint32_t>(
        CALL_GENERATED_CODE(isolate, f, p0, p1, 0, 0, 0));
    Float64 exp = Float64::FromBits(f64.get_bits() & ~(1ull << 63));
    // We just get back the top word, so only compare that one.
    CHECK_EQ(exp.get_bits() >> 32, res);
  }
}

TEST(simulator_vneg_32) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  F_iiiii f = FUNCTION_CAST<F_iiiii>(AssembleCode([](Assembler& assm) {
    __ vmov(s0, r0);
    __ vneg(s0, s0);
    __ vmov(r0, s0);
  }));

  for (Float32 f32 : Float32Inputs()) {
    Float32 res = Float32::FromBits(reinterpret_cast<uint32_t>(
        CALL_GENERATED_CODE(isolate, f, f32.get_bits(), 0, 0, 0, 0)));
    Float32 exp = Float32::FromBits(f32.get_bits() ^ (1 << 31));
    CHECK_EQ(exp.get_bits(), res.get_bits());
  }
}

TEST(simulator_vneg_64) {
  Isolate* isolate = CcTest::i_isolate();
  HandleScope scope(isolate);

  F_iiiii f = FUNCTION_CAST<F_iiiii>(AssembleCode([](Assembler& assm) {
    __ vmov(d0, r0, r1);
    __ vneg(d0, d0);
    __ vmov(r1, r0, d0);
  }));

  for (Float64 f64 : Float64Inputs()) {
    uint32_t p0 = static_cast<uint32_t>(f64.get_bits());
    uint32_t p1 = static_cast<uint32_t>(f64.get_bits() >> 32);
    uint32_t res = reinterpret_cast<uint32_t>(
        CALL_GENERATED_CODE(isolate, f, p0, p1, 0, 0, 0));
    Float64 exp = Float64::FromBits(f64.get_bits() ^ (1ull << 63));
    // We just get back the top word, so only compare that one.
    CHECK_EQ(exp.get_bits() >> 32, res);
  }
}

#undef __

}  // namespace internal
}  // namespace v8
