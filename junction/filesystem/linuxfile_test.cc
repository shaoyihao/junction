#include "junction/filesystem/linuxfile.h"

#include <fcntl.h>
#include <gtest/gtest.h>

#include <memory>
#include <span>
#include <string>
#include <string_view>

#include "junction/base/io.h"

using namespace junction;

class LinuxFileTest : public ::testing::Test {};

TEST_F(LinuxFileTest, FileOpenTest) {
  // Inputs/Outputs
  const std::string filepath = "testdata/test.txt";
  const unsigned int flags = 0;
  const unsigned int mode = kModeRead;

  // Action
  std::shared_ptr<LinuxFile> lf = LinuxFile::Open(filepath, flags, mode);

  // Test
  EXPECT_NE(nullptr, lf);
  EXPECT_EQ(flags, lf->get_flags());
  EXPECT_NE(-1, lf->get_fd());
}

TEST_F(LinuxFileTest, FileReadTest) {
  // Inputs/Outputs
  const std::string filepath = "testdata/test.txt";
  const unsigned int flags = 0;
  const unsigned int mode = kModeRead;
  const std::string data = "foo";
  const size_t nbytes = data.size();

  auto buf = std::make_unique<char[]>(nbytes);
  off_t offset{0};

  // Action
  std::shared_ptr<LinuxFile> lf = LinuxFile::Open(filepath, flags, mode);
  auto ret = lf->Read(readable_span(buf.get(), nbytes), &offset);

  // Test
  EXPECT_TRUE(ret);
  EXPECT_EQ(nbytes, ret.value());
  EXPECT_EQ(nbytes, offset);
  EXPECT_EQ(data, std::string(buf.get(), nbytes));
}

TEST_F(LinuxFileTest, FileWriteTest) {
  // Inputs/Outputs
  const std::string filepath = "/tmp";
  const unsigned int flags = kFlagTemp | kModeReadWrite;
  const unsigned int mode = S_IRWXU | S_IRWXG | S_IRWXO;
  const std::string data = "foobar";
  const size_t nbytes = data.size();

  off_t offset{0};

  // Action (Write)
  std::shared_ptr<LinuxFile> lf = LinuxFile::Open(filepath, flags, mode);
  auto ret = lf->Write(writable_span(data.c_str(), nbytes), &offset);

  // Test
  EXPECT_TRUE(ret);
  EXPECT_EQ(nbytes, ret.value());
  EXPECT_EQ(nbytes, offset);

  // Inputs/Outputs
  auto read_buf = std::make_unique<char[]>(nbytes);
  offset = 0;

  // Action (Read)
  ret = lf->Read(readable_span(read_buf.get(), nbytes), &offset);

  // Test
  EXPECT_TRUE(ret);
  EXPECT_EQ(nbytes, ret.value());
  EXPECT_EQ(nbytes, offset);
  EXPECT_EQ(data, std::string(read_buf.get(), nbytes));
}

TEST_F(LinuxFileTest, FileWriteInvalidModeTest) { GTEST_SKIP() << "Skipping"; }

TEST_F(LinuxFileTest, FileReadNonExistentTest) { GTEST_SKIP() << "Skipping"; }