/**
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * This source code is licensed under the MIT license found in the LICENSE
 * file in the root directory of this source tree.
 */
#include "hermes/BCGen/HBC/BytecodeProviderFromSrc.h"
#include "hermes/Support/MemoryBuffer.h"
#include "hermes/VM/Runtime.h"

#include "TestHelpers.h"

#include "gtest/gtest.h"

#include <string>

using namespace hermes;
using namespace hermes::vm;
using namespace hermes::hbc;

namespace {

/// Assert that obj.prop is frozen and the static builtin flag is marked.
#define EXPECT_PROPERTY_FROZEN_AND_MARKED_AS_STATIC(obj, prop)              \
  {                                                                         \
    NamedPropertyDescriptor desc;                                           \
    ASSERT_TRUE(                                                            \
        JSObject::getNamedDescriptor(obj, runtime, prop, desc) != nullptr); \
    EXPECT_FALSE(desc.flags.writable);                                      \
    EXPECT_FALSE(desc.flags.configurable);                                  \
    EXPECT_TRUE(desc.flags.staticBuiltin);                                  \
  }

/// Verify builtin objects and the builtin methods on those objects
/// are frozen and marked as static builtins.
static void verifyAllBuiltinsFrozen(Runtime *runtime) {
  GCScope gcScope{runtime};
  auto global = runtime->getGlobal();
  runtime->getPredefinedSymbolID(Predefined::isArray);
#define BUILTIN_OBJECT(object)                                          \
  {                                                                     \
    auto objectID = runtime->getPredefinedSymbolID(Predefined::object); \
    EXPECT_PROPERTY_FROZEN_AND_MARKED_AS_STATIC(global, objectID)       \
  }

#define BUILTIN_METHOD(object, method)                                  \
  {                                                                     \
    auto objectID = runtime->getPredefinedSymbolID(Predefined::object); \
    auto cr = JSObject::getNamed(global, runtime, objectID);            \
    ASSERT_NE(cr, ExecutionStatus::EXCEPTION);                          \
    auto objHandle = runtime->makeHandle<JSObject>(*cr);                \
    auto methodID = runtime->getPredefinedSymbolID(Predefined::method); \
    EXPECT_PROPERTY_FROZEN_AND_MARKED_AS_STATIC(objHandle, methodID);   \
  }
#include "hermes/Inst/Builtins.def"
}

using StaticBuiltinsTest = RuntimeTestFixture;

TEST_F(StaticBuiltinsTest, FreezeBuiltins) {
  std::string testCode = R"(
    function assert(condition) {
      if (!condition) {
        throw Error();
      }
    }
    assert(Array.isArray([1, 2]));
    assert(Math.abs(-1) === 1);
    assert(JSON.stringify(Object.keys({a:1, b: 2})) === '["a","b"]');
    assert(String.fromCharCode(65, 66, 67) === "ABC");
  )";

  // run normal bytecode first
  CompileFlags flagsNone;
  flagsNone.staticBuiltins = false;
  EXPECT_EQ(
      runtime->run(testCode, "source/url", flagsNone),
      ExecutionStatus::RETURNED);
  EXPECT_FALSE(runtime->builtinsAreFrozen());

  // run bytecode with static builtins
  CompileFlags flagsBuiltin;
  flagsBuiltin.staticBuiltins = true;
  EXPECT_EQ(
      runtime->run(testCode, "source/url", flagsBuiltin),
      ExecutionStatus::RETURNED);
  EXPECT_TRUE(runtime->builtinsAreFrozen());
  verifyAllBuiltinsFrozen(runtime);

  // run normal bytecode again
  EXPECT_EQ(
      runtime->run(testCode, "source/url", flagsNone),
      ExecutionStatus::RETURNED);
  EXPECT_TRUE(runtime->builtinsAreFrozen());

  // run bytecode with builtins again
  EXPECT_EQ(
      runtime->run(testCode, "source/url", flagsNone),
      ExecutionStatus::RETURNED);
  EXPECT_TRUE(runtime->builtinsAreFrozen());

  verifyAllBuiltinsFrozen(runtime);
}

TEST_F(StaticBuiltinsTest, BuiltinsOverridden) {
  // run bytecode that overrides a builtin method
  std::string codeChangeBuiltin = R"(
    function assert(condition) {
      if (!condition) {
        throw Error();
      }
    }
    Array.isArray = true;
    assert(Array.isArray);
  )";
  CompileFlags flagsNone;
  flagsNone.staticBuiltins = false;
  EXPECT_EQ(
      runtime->run(codeChangeBuiltin, "source/url", flagsNone),
      ExecutionStatus::RETURNED);
  EXPECT_FALSE(runtime->builtinsAreFrozen());

  // run bytecode compiled with static builtins enabled
  std::string codeStaticBuiltin = R"(
    print("hello world");
  )";
  CompileFlags flagsBuiltin;
  flagsBuiltin.staticBuiltins = true;
  EXPECT_EQ(
      runtime->run(codeStaticBuiltin, "source/url", flagsBuiltin),
      ExecutionStatus::EXCEPTION);
}

TEST_F(StaticBuiltinsTest, AttemptToOverrideBuiltins) {
  // Run bytecode compiled with static builtins enabled but attempt to override
  // a builtin method at the same time. We should get an exception.
  std::string codeStaticBuiltin = R"(
    Math.sin = false;
  )";
  CompileFlags flagsBuiltin;
  flagsBuiltin.staticBuiltins = true;
  EXPECT_EQ(
      runtime->run(codeStaticBuiltin, "source/url", flagsBuiltin),
      ExecutionStatus::EXCEPTION);
}

TEST_F(StaticBuiltinsTest, AttemptToOverrideBuiltins2) {
  // Run bytecode compiled with static builtins enabled to freeze the builtins.
  std::string codeStaticBuiltin = R"(
    print('hello world');
  )";
  CompileFlags flagsBuiltin;
  flagsBuiltin.staticBuiltins = true;
  EXPECT_EQ(
      runtime->run(codeStaticBuiltin, "source/url", flagsBuiltin),
      ExecutionStatus::RETURNED);

  // Run bytecode that attempts to override a builtin method. We should get an
  // exception.
  std::string codeOverrideBuiltin = R"(
    Math.sin = false;
  )";
  CompileFlags flagsNone;
  flagsNone.staticBuiltins = false;
  EXPECT_EQ(
      runtime->run(codeOverrideBuiltin, "source/url", flagsNone),
      ExecutionStatus::EXCEPTION);
}

} // namespace
