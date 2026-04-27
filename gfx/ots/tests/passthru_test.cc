// Copyright (c) 2024 The OTS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Regression tests for https://github.com/khaledhosny/ots/issues/308
// Null pointer dereference in ProcessGeneric() with TABLE_ACTION_PASSTHRU.

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "opentype-sanitiser.h"
#include "ots-memory-stream.h"

namespace {

// ---------------------------------------------------------------------------
// Helper: read a file into a string.
// ---------------------------------------------------------------------------
std::string ReadFile(const char* path) {
  std::ifstream f(path, std::ifstream::binary);
  if (!f.good())
    return "";
  return std::string((std::istreambuf_iterator<char>(f)),
                     (std::istreambuf_iterator<char>()));
}

// Helper: process font data with a given context. Returns true on success.
// The key regression assertion is that this does NOT crash (SEGV/abort).
bool ProcessFont(ots::OTSContext& context,
                 const std::string& font_data) {
  ots::ExpandingMemoryStream stream(font_data.size() + 1,
                                    font_data.size() * 8 + 1);
  return context.Process(&stream,
                         reinterpret_cast<const uint8_t*>(font_data.data()),
                         font_data.size());
}

// ---------------------------------------------------------------------------
// Test contexts — each overrides GetTableAction differently.
// ---------------------------------------------------------------------------

// All tables use passthrough (the primary bug trigger from issue #308).
class AllPassthruContext : public ots::OTSContext {
 public:
  void Message(int, const char*, ...) {}
  ots::TableAction GetTableAction(uint32_t) override {
    return ots::TABLE_ACTION_PASSTHRU;
  }
};

// Only maxp is passthrough, all others default.
class MaxpPassthruContext : public ots::OTSContext {
 public:
  void Message(int, const char*, ...) {}
  ots::TableAction GetTableAction(uint32_t tag) override {
    if (tag == OTS_TAG('m', 'a', 'x', 'p'))
      return ots::TABLE_ACTION_PASSTHRU;
    return ots::TABLE_ACTION_DEFAULT;
  }
};

// Only the name table is passthrough, all others default.
// Exercises the glyf.cc null dereference path (name->IsTrickyFont()).
class NamePassthruContext : public ots::OTSContext {
 public:
  void Message(int, const char*, ...) {}
  ots::TableAction GetTableAction(uint32_t tag) override {
    if (tag == OTS_TAG('n', 'a', 'm', 'e'))
      return ots::TABLE_ACTION_PASSTHRU;
    return ots::TABLE_ACTION_DEFAULT;
  }
};

// Both maxp and name are passthrough — exercises both null-pointer paths.
class MaxpAndNamePassthruContext : public ots::OTSContext {
 public:
  void Message(int, const char*, ...) {}
  ots::TableAction GetTableAction(uint32_t tag) override {
    if (tag == OTS_TAG('m', 'a', 'x', 'p') ||
        tag == OTS_TAG('n', 'a', 'm', 'e'))
      return ots::TABLE_ACTION_PASSTHRU;
    return ots::TABLE_ACTION_DEFAULT;
  }
};

// Outline tables (glyf/loca/cff/cff2) passthrough, maxp is default/sanitized.
// maxp is a real OpenTypeMAXP → GetTypedTable returns non-null → safe.
class OutlinesPassthruContext : public ots::OTSContext {
 public:
  void Message(int, const char*, ...) {}
  ots::TableAction GetTableAction(uint32_t tag) override {
    if (tag == OTS_TAG('g', 'l', 'y', 'f') ||
        tag == OTS_TAG('l', 'o', 'c', 'a') ||
        tag == OTS_TAG('C', 'F', 'F', ' ') ||
        tag == OTS_TAG('C', 'F', 'F', '2'))
      return ots::TABLE_ACTION_PASSTHRU;
    return ots::TABLE_ACTION_DEFAULT;
  }
};

// maxp is dropped entirely, everything else is default.
class MaxpDropContext : public ots::OTSContext {
 public:
  void Message(int, const char*, ...) {}
  ots::TableAction GetTableAction(uint32_t tag) override {
    if (tag == OTS_TAG('m', 'a', 'x', 'p'))
      return ots::TABLE_ACTION_DROP;
    return ots::TABLE_ACTION_DEFAULT;
  }
};

// All tables are dropped.
class AllDropContext : public ots::OTSContext {
 public:
  void Message(int, const char*, ...) {}
  ots::TableAction GetTableAction(uint32_t) override {
    return ots::TABLE_ACTION_DROP;
  }
};

// All tables explicitly sanitized (equivalent to default, but explicit).
class AllSanitizeContext : public ots::OTSContext {
 public:
  void Message(int, const char*, ...) {}
  ots::TableAction GetTableAction(uint32_t) override {
    return ots::TABLE_ACTION_SANITIZE;
  }
};

// maxp is passthrough, outline tables are passthrough too.
// Since both sides are passthrough, neither GetTypedTable call returns
// a proper typed pointer.
class MaxpAndOutlinesPassthruContext : public ots::OTSContext {
 public:
  void Message(int, const char*, ...) {}
  ots::TableAction GetTableAction(uint32_t tag) override {
    if (tag == OTS_TAG('m', 'a', 'x', 'p') ||
        tag == OTS_TAG('g', 'l', 'y', 'f') ||
        tag == OTS_TAG('l', 'o', 'c', 'a') ||
        tag == OTS_TAG('C', 'F', 'F', ' ') ||
        tag == OTS_TAG('C', 'F', 'F', '2'))
      return ots::TABLE_ACTION_PASSTHRU;
    return ots::TABLE_ACTION_DEFAULT;
  }
};

// Only head is passthrough — a required table but not one directly involved
// in the maxp/name bug paths. Tests that passthrough of other critical tables
// doesn't cause unrelated crashes.
class HeadPassthruContext : public ots::OTSContext {
 public:
  void Message(int, const char*, ...) {}
  ots::TableAction GetTableAction(uint32_t tag) override {
    if (tag == OTS_TAG('h', 'e', 'a', 'd'))
      return ots::TABLE_ACTION_PASSTHRU;
    return ots::TABLE_ACTION_DEFAULT;
  }
};

// Use SANITIZE_SOFT for maxp — should not crash.
class MaxpSoftSanitizeContext : public ots::OTSContext {
 public:
  void Message(int, const char*, ...) {}
  ots::TableAction GetTableAction(uint32_t tag) override {
    if (tag == OTS_TAG('m', 'a', 'x', 'p'))
      return ots::TABLE_ACTION_SANITIZE_SOFT;
    return ots::TABLE_ACTION_DEFAULT;
  }
};

// ---------------------------------------------------------------------------
// Test fixture that loads the test font from the environment.
// ---------------------------------------------------------------------------
class PassthruTest : public ::testing::Test {
 protected:
  void SetUp() override {
    const char* path = std::getenv("OTS_TEST_FONT");
    ASSERT_NE(path, nullptr) << "OTS_TEST_FONT environment variable not set";
    font_data_ = ReadFile(path);
    ASSERT_FALSE(font_data_.empty()) << "Failed to read font: " << path;
  }

  std::string font_data_;
};

// ===========================================================================
// PRIMARY REGRESSION TEST: Issue #308
// All tables set to passthrough causes null maxp dereference in
// ProcessGeneric() at ots.cc:781/798/800.
// ===========================================================================
TEST_F(PassthruTest, AllPassthruDoesNotCrash) {
  AllPassthruContext context;
  // Must not crash. Return value is secondary — no SEGV is the goal.
  EXPECT_NO_FATAL_FAILURE(ProcessFont(context, font_data_));
}

// ===========================================================================
// SELECTIVE PASSTHROUGH: maxp only
// GetTypedTable(OTS_TAG_MAXP) returns NULL when maxp is TablePassthru
// because Type()==0 != OTS_TAG_MAXP. The glyf/loca or cff/cff2 branch
// in ProcessGeneric() dereferences it.
// ===========================================================================
TEST_F(PassthruTest, MaxpPassthruDoesNotCrash) {
  MaxpPassthruContext context;
  bool result = false;
  EXPECT_NO_FATAL_FAILURE(result = ProcessFont(context, font_data_));
  // maxp is required for outline processing — should fail, not crash.
  EXPECT_FALSE(result);
}

// ===========================================================================
// SECONDARY FIX: name table passthrough
// glyf.cc calls GetTypedTable(OTS_TAG_NAME) → name->IsTrickyFont().
// If name is passthrough, the typed lookup returns NULL → crash.
// ===========================================================================
TEST_F(PassthruTest, NamePassthruDoesNotCrash) {
  NamePassthruContext context;
  bool result = false;
  EXPECT_NO_FATAL_FAILURE(result = ProcessFont(context, font_data_));
  // name is needed by glyf for IsTrickyFont() check — should fail gracefully.
  EXPECT_FALSE(result);
}

// ===========================================================================
// COMBINED: both maxp and name passthrough
// Exercises both null-pointer code paths simultaneously.
// ===========================================================================
TEST_F(PassthruTest, MaxpAndNamePassthruDoesNotCrash) {
  MaxpAndNamePassthruContext context;
  bool result = false;
  EXPECT_NO_FATAL_FAILURE(result = ProcessFont(context, font_data_));
  EXPECT_FALSE(result);
}

// ===========================================================================
// SAFE CONFIGURATION: outline tables passthrough, maxp sanitized (default).
// maxp is a proper OpenTypeMAXP → GetTypedTable returns non-null.
// This configuration should NOT be broken by the fix.
// ===========================================================================
TEST_F(PassthruTest, OutlinesPassthruMaxpDefaultDoesNotCrash) {
  OutlinesPassthruContext context;
  // Should not crash. May succeed or fail depending on internal checks,
  // but the maxp dereference path is safe.
  EXPECT_NO_FATAL_FAILURE(ProcessFont(context, font_data_));
}

// ===========================================================================
// maxp AND outlines all passthrough.
// Both sides are TablePassthru. glyf/loca are found via GetTable() (not
// GetTypedTable), so they resolve as TablePassthru OK. But maxp via
// GetTypedTable returns NULL.
// ===========================================================================
TEST_F(PassthruTest, MaxpAndOutlinesPassthruDoesNotCrash) {
  MaxpAndOutlinesPassthruContext context;
  EXPECT_NO_FATAL_FAILURE(ProcessFont(context, font_data_));
}

// ===========================================================================
// maxp dropped — GetTable returns NULL for maxp entirely.
// Similar to passthrough NULL path but triggered by DROP action.
// ===========================================================================
TEST_F(PassthruTest, MaxpDropDoesNotCrash) {
  MaxpDropContext context;
  bool result = false;
  EXPECT_NO_FATAL_FAILURE(result = ProcessFont(context, font_data_));
  EXPECT_FALSE(result);
}

// ===========================================================================
// All tables dropped — no glyph data tables at all.
// Should hit the "no supported glyph data table(s) present" error.
// ===========================================================================
TEST_F(PassthruTest, AllDropDoesNotCrash) {
  AllDropContext context;
  bool result = false;
  EXPECT_NO_FATAL_FAILURE(result = ProcessFont(context, font_data_));
  EXPECT_FALSE(result);
}

// ===========================================================================
// REGRESSION: default processing still works after the fix.
// ===========================================================================
TEST_F(PassthruTest, DefaultProcessingStillWorks) {
  ots::OTSContext context;
  EXPECT_TRUE(ProcessFont(context, font_data_));
}

// ===========================================================================
// Explicit TABLE_ACTION_SANITIZE does not crash.
// Note: TABLE_ACTION_SANITIZE may reject tables that TABLE_ACTION_DEFAULT
// would handle differently (e.g., bitmap tables like CBDT/CBLC that default
// mode passes through). So we only assert no crash, not success.
// ===========================================================================
TEST_F(PassthruTest, ExplicitSanitizeDoesNotCrash) {
  AllSanitizeContext context;
  EXPECT_NO_FATAL_FAILURE(ProcessFont(context, font_data_));
}

// ===========================================================================
// head table passthrough — tests that passthrough of other critical tables
// does not cause crashes in unrelated code paths.
// ===========================================================================
TEST_F(PassthruTest, HeadPassthruDoesNotCrash) {
  HeadPassthruContext context;
  EXPECT_NO_FATAL_FAILURE(ProcessFont(context, font_data_));
}

// ===========================================================================
// TABLE_ACTION_SANITIZE_SOFT for maxp — not the same as passthrough,
// so GetTypedTable should work. Verifies we don't over-match.
// ===========================================================================
TEST_F(PassthruTest, MaxpSoftSanitizeDoesNotCrash) {
  MaxpSoftSanitizeContext context;
  EXPECT_NO_FATAL_FAILURE(ProcessFont(context, font_data_));
}

// ===========================================================================
// Sequential processing with different contexts.
// Verifies no global state corruption between runs.
// ===========================================================================
TEST_F(PassthruTest, SequentialDifferentContextsNoCorruption) {
  // First: all passthrough (should not crash)
  {
    AllPassthruContext ctx;
    EXPECT_NO_FATAL_FAILURE(ProcessFont(ctx, font_data_));
  }
  // Second: default sanitize (should succeed — no residual corruption)
  {
    ots::OTSContext ctx;
    EXPECT_TRUE(ProcessFont(ctx, font_data_));
  }
  // Third: maxp passthrough (should not crash)
  {
    MaxpPassthruContext ctx;
    EXPECT_NO_FATAL_FAILURE(ProcessFont(ctx, font_data_));
  }
  // Fourth: default again (should still succeed)
  {
    ots::OTSContext ctx;
    EXPECT_TRUE(ProcessFont(ctx, font_data_));
  }
}

// ===========================================================================
// BOUNDARY: empty input in passthrough mode.
// ===========================================================================
TEST(PassthruBoundaryTest, EmptyInputDoesNotCrash) {
  AllPassthruContext context;
  std::string empty;
  bool result = false;
  EXPECT_NO_FATAL_FAILURE(result = ProcessFont(context, empty));
  EXPECT_FALSE(result);
}

// ===========================================================================
// BOUNDARY: truncated input (partial sfnt header) in passthrough mode.
// ===========================================================================
TEST(PassthruBoundaryTest, TruncatedHeaderDoesNotCrash) {
  AllPassthruContext context;
  // 4 bytes — a valid-looking TrueType signature but no table directory.
  std::string truncated("\x00\x01\x00\x00", 4);
  bool result = false;
  EXPECT_NO_FATAL_FAILURE(result = ProcessFont(context, truncated));
  EXPECT_FALSE(result);
}

// ===========================================================================
// BOUNDARY: garbage data in passthrough mode.
// ===========================================================================
TEST(PassthruBoundaryTest, GarbageInputDoesNotCrash) {
  AllPassthruContext context;
  std::string garbage(256, '\xFF');
  bool result = false;
  EXPECT_NO_FATAL_FAILURE(result = ProcessFont(context, garbage));
  EXPECT_FALSE(result);
}

// ===========================================================================
// BOUNDARY: minimal sfnt header with a maxp table record that points
// past end of data. Tests that passthrough mode handles table read failures.
// ===========================================================================
TEST(PassthruBoundaryTest, MinimalHeaderWithBadMaxpOffset) {
  AllPassthruContext context;
  // TrueType sfnt header + 1 table record for maxp
  const uint8_t data[] = {
    0x00, 0x01, 0x00, 0x00,  // sfVersion (TrueType)
    0x00, 0x01,              // numTables = 1
    0x00, 0x10,              // searchRange
    0x00, 0x00,              // entrySelector
    0x00, 0x10,              // rangeShift
    // Table record:
    'm',  'a',  'x',  'p',  // tag
    0x00, 0x00, 0x00, 0x00,  // checksum
    0x00, 0x00, 0x01, 0x00,  // offset = 256 (past end)
    0x00, 0x00, 0x00, 0x06,  // length = 6
  };
  std::string font_data(reinterpret_cast<const char*>(data), sizeof(data));
  bool result = false;
  EXPECT_NO_FATAL_FAILURE(result = ProcessFont(context, font_data));
  EXPECT_FALSE(result);
}

// ===========================================================================
// BOUNDARY: single byte input.
// ===========================================================================
TEST(PassthruBoundaryTest, SingleByteDoesNotCrash) {
  AllPassthruContext context;
  std::string one_byte("\x00", 1);
  bool result = false;
  EXPECT_NO_FATAL_FAILURE(result = ProcessFont(context, one_byte));
  EXPECT_FALSE(result);
}

// ===========================================================================
// BOUNDARY: maxp passthrough with empty input.
// ===========================================================================
TEST(PassthruBoundaryTest, MaxpPassthruEmptyInputDoesNotCrash) {
  MaxpPassthruContext context;
  std::string empty;
  bool result = false;
  EXPECT_NO_FATAL_FAILURE(result = ProcessFont(context, empty));
  EXPECT_FALSE(result);
}

// ===========================================================================
// BOUNDARY: name passthrough with empty input.
// ===========================================================================
TEST(PassthruBoundaryTest, NamePassthruEmptyInputDoesNotCrash) {
  NamePassthruContext context;
  std::string empty;
  bool result = false;
  EXPECT_NO_FATAL_FAILURE(result = ProcessFont(context, empty));
  EXPECT_FALSE(result);
}

}  // namespace
