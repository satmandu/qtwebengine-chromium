// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/edk/embedder/embedder.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <utility>

#include "base/base_paths.h"
#include "base/bind.h"
#include "base/command_line.h"
#include "base/files/file.h"
#include "base/logging.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/memory/shared_memory.h"
#include "base/message_loop/message_loop.h"
#include "base/path_service.h"
#include "base/process/process_handle.h"
#include "base/run_loop.h"
#include "base/synchronization/waitable_event.h"
#include "base/test/test_timeouts.h"
#include "mojo/edk/embedder/embedder.h"
#include "mojo/edk/embedder/named_platform_handle.h"
#include "mojo/edk/embedder/named_platform_handle_utils.h"
#include "mojo/edk/embedder/pending_process_connection.h"
#include "mojo/edk/embedder/platform_channel_pair.h"
#include "mojo/edk/embedder/test_embedder.h"
#include "mojo/edk/system/test_utils.h"
#include "mojo/edk/test/mojo_test_base.h"
#include "mojo/public/c/system/core.h"
#include "mojo/public/cpp/system/handle.h"
#include "mojo/public/cpp/system/message_pipe.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace mojo {
namespace edk {
namespace {

const MojoHandleSignals kSignalReadadableWritable =
    MOJO_HANDLE_SIGNAL_READABLE | MOJO_HANDLE_SIGNAL_WRITABLE;

const MojoHandleSignals kSignalAll = MOJO_HANDLE_SIGNAL_READABLE |
                                     MOJO_HANDLE_SIGNAL_WRITABLE |
                                     MOJO_HANDLE_SIGNAL_PEER_CLOSED;

// The multiprocess tests that use these don't compile on iOS.
#if !defined(OS_IOS)
const char kHelloWorld[] = "hello world";
const char kByeWorld[] = "bye world";
#endif

using EmbedderTest = test::MojoTestBase;

TEST_F(EmbedderTest, ChannelBasic) {
  MojoHandle server_mp, client_mp;
  CreateMessagePipe(&server_mp, &client_mp);

  const std::string kHello = "hello";

  // We can write to a message pipe handle immediately.
  WriteMessage(server_mp, kHello);
  EXPECT_EQ(kHello, ReadMessage(client_mp));

  ASSERT_EQ(MOJO_RESULT_OK, MojoClose(server_mp));
  ASSERT_EQ(MOJO_RESULT_OK, MojoClose(client_mp));
}

// Test sending a MP which has read messages out of the OS pipe but which have
// not been consumed using MojoReadMessage yet.
TEST_F(EmbedderTest, SendReadableMessagePipe) {
  MojoHandle server_mp, client_mp;
  CreateMessagePipe(&server_mp, &client_mp);

  MojoHandle server_mp2, client_mp2;
  CreateMessagePipe(&server_mp2, &client_mp2);

  // Write to server2 and wait for client2 to be readable before sending it.
  // client2's MessagePipeDispatcher will have the message below in its
  // message_queue_. For extra measures, also verify that this pending message
  // can contain a message pipe.
  MojoHandle server_mp3, client_mp3;
  CreateMessagePipe(&server_mp3, &client_mp3);

  const std::string kHello = "hello";
  WriteMessageWithHandles(server_mp2, kHello, &client_mp3, 1);

  MojoHandleSignalsState state;
  ASSERT_EQ(MOJO_RESULT_OK, MojoWait(client_mp2, MOJO_HANDLE_SIGNAL_READABLE,
                                     MOJO_DEADLINE_INDEFINITE, &state));
  ASSERT_EQ(kSignalReadadableWritable, state.satisfied_signals);
  ASSERT_EQ(kSignalAll, state.satisfiable_signals);

  // Now send client2
  WriteMessageWithHandles(server_mp, kHello, &client_mp2, 1);

  MojoHandle port;
  std::string message = ReadMessageWithHandles(client_mp, &port, 1);
  EXPECT_EQ(kHello, message);

  client_mp2 = port;
  message = ReadMessageWithHandles(client_mp2, &client_mp3, 1);
  EXPECT_EQ(kHello, message);

  ASSERT_EQ(MOJO_RESULT_OK, MojoClose(server_mp3));
  ASSERT_EQ(MOJO_RESULT_OK, MojoClose(client_mp3));
  ASSERT_EQ(MOJO_RESULT_OK, MojoClose(server_mp2));
  ASSERT_EQ(MOJO_RESULT_OK, MojoClose(client_mp2));
  ASSERT_EQ(MOJO_RESULT_OK, MojoClose(server_mp));
  ASSERT_EQ(MOJO_RESULT_OK, MojoClose(client_mp));
}

// Verifies that a MP with pending messages to be written can be sent and the
// pending messages aren't dropped.
TEST_F(EmbedderTest, SendMessagePipeWithWriteQueue) {
  MojoHandle server_mp, client_mp;
  CreateMessagePipe(&server_mp, &client_mp);

  MojoHandle server_mp2, client_mp2;
  CreateMessagePipe(&server_mp2, &client_mp2);

  static const size_t kNumMessages = 1001;
  for (size_t i = 1; i <= kNumMessages; i++)
    WriteMessage(client_mp2, std::string(i, 'A' + (i % 26)));

  // Now send client2.
  WriteMessageWithHandles(server_mp, "hey", &client_mp2, 1);
  client_mp2 = MOJO_HANDLE_INVALID;

  // Read client2 just so we can close it later.
  EXPECT_EQ("hey", ReadMessageWithHandles(client_mp, &client_mp2, 1));
  EXPECT_NE(MOJO_HANDLE_INVALID, client_mp2);

  // Now verify that all the messages that were written were sent correctly.
  for (size_t i = 1; i <= kNumMessages; i++)
    ASSERT_EQ(std::string(i, 'A' + (i % 26)), ReadMessage(server_mp2));

  ASSERT_EQ(MOJO_RESULT_OK, MojoClose(server_mp2));
  ASSERT_EQ(MOJO_RESULT_OK, MojoClose(client_mp2));
  ASSERT_EQ(MOJO_RESULT_OK, MojoClose(server_mp));
  ASSERT_EQ(MOJO_RESULT_OK, MojoClose(client_mp));
}

TEST_F(EmbedderTest, ChannelsHandlePassing) {
  MojoHandle server_mp, client_mp;
  CreateMessagePipe(&server_mp, &client_mp);
  EXPECT_NE(server_mp, MOJO_HANDLE_INVALID);
  EXPECT_NE(client_mp, MOJO_HANDLE_INVALID);

  MojoHandle h0, h1;
  CreateMessagePipe(&h0, &h1);

  // Write a message to |h0| (attaching nothing).
  const std::string kHello = "hello";
  WriteMessage(h0, kHello);

  // Write one message to |server_mp|, attaching |h1|.
  const std::string kWorld = "world!!!";
  WriteMessageWithHandles(server_mp, kWorld, &h1, 1);
  h1 = MOJO_HANDLE_INVALID;

  // Write another message to |h0|.
  const std::string kFoo = "foo";
  WriteMessage(h0, kFoo);

  // Wait for |client_mp| to become readable and read a message from it.
  EXPECT_EQ(kWorld, ReadMessageWithHandles(client_mp, &h1, 1));
  EXPECT_NE(h1, MOJO_HANDLE_INVALID);

  // Wait for |h1| to become readable and read a message from it.
  EXPECT_EQ(kHello, ReadMessage(h1));

  // Wait for |h1| to become readable (again) and read its second message.
  EXPECT_EQ(kFoo, ReadMessage(h1));

  // Write a message to |h1|.
  const std::string kBarBaz = "barbaz";
  WriteMessage(h1, kBarBaz);

  // Wait for |h0| to become readable and read a message from it.
  EXPECT_EQ(kBarBaz, ReadMessage(h0));

  ASSERT_EQ(MOJO_RESULT_OK, MojoClose(server_mp));
  ASSERT_EQ(MOJO_RESULT_OK, MojoClose(client_mp));
  ASSERT_EQ(MOJO_RESULT_OK, MojoClose(h0));
  ASSERT_EQ(MOJO_RESULT_OK, MojoClose(h1));
}

TEST_F(EmbedderTest, PipeSetup) {
  // Ensures that a pending process connection's message pipe can be claimed by
  // the host process itself.
  PendingProcessConnection process;
  std::string pipe_token;
  ScopedMessagePipeHandle parent_mp = process.CreateMessagePipe(&pipe_token);
  ScopedMessagePipeHandle child_mp = CreateChildMessagePipe(pipe_token);

  const std::string kHello = "hello";
  WriteMessage(parent_mp.get().value(), kHello);

  EXPECT_EQ(kHello, ReadMessage(child_mp.get().value()));
}

TEST_F(EmbedderTest, PipeSetup_LaunchDeath) {
  PlatformChannelPair pair;

  PendingProcessConnection process;
  std::string pipe_token;
  ScopedMessagePipeHandle parent_mp = process.CreateMessagePipe(&pipe_token);
  process.Connect(base::GetCurrentProcessHandle(), pair.PassServerHandle());

  // Close the remote end, simulating child death before the child connects to
  // the reserved port.
  ignore_result(pair.PassClientHandle());

  EXPECT_EQ(MOJO_RESULT_OK, MojoWait(parent_mp.get().value(),
                                     MOJO_HANDLE_SIGNAL_PEER_CLOSED,
                                     MOJO_DEADLINE_INDEFINITE,
                                     nullptr));
}

TEST_F(EmbedderTest, PipeSetup_LaunchFailure) {
  PlatformChannelPair pair;

  auto process = base::MakeUnique<PendingProcessConnection>();
  std::string pipe_token;
  ScopedMessagePipeHandle parent_mp = process->CreateMessagePipe(&pipe_token);

  // Ensure that if a PendingProcessConnection goes away before Connect() is
  // called, any message pipes associated with it detect peer closure.
  process.reset();

  EXPECT_EQ(MOJO_RESULT_OK, MojoWait(parent_mp.get().value(),
                                     MOJO_HANDLE_SIGNAL_PEER_CLOSED,
                                     MOJO_DEADLINE_INDEFINITE,
                                     nullptr));
}

// The sequence of messages sent is:
//       server_mp   client_mp   mp0         mp1         mp2         mp3
//   1.  "hello"
//   2.              "world!"
//   3.                          "FOO"
//   4.  "Bar"+mp1
//   5.  (close)
//   6.              (close)
//   7.                                                              "baz"
//   8.                                                              (closed)
//   9.                                      "quux"+mp2
//  10.                          (close)
//  11.                                      (wait/cl.)
//  12.                                                  (wait/cl.)

#if !defined(OS_IOS)

TEST_F(EmbedderTest, MultiprocessChannels) {
  RUN_CHILD_ON_PIPE(MultiprocessChannelsClient, server_mp)
    // 1. Write a message to |server_mp| (attaching nothing).
    WriteMessage(server_mp, "hello");

    // 2. Read a message from |server_mp|.
    EXPECT_EQ("world!", ReadMessage(server_mp));

    // 3. Create a new message pipe (endpoints |mp0| and |mp1|).
    MojoHandle mp0, mp1;
    CreateMessagePipe(&mp0, &mp1);

    // 4. Write something to |mp0|.
    WriteMessage(mp0, "FOO");

    // 5. Write a message to |server_mp|, attaching |mp1|.
    WriteMessageWithHandles(server_mp, "Bar", &mp1, 1);
    mp1 = MOJO_HANDLE_INVALID;

    // 6. Read a message from |mp0|, which should have |mp2| attached.
    MojoHandle mp2 = MOJO_HANDLE_INVALID;
    EXPECT_EQ("quux", ReadMessageWithHandles(mp0, &mp2, 1));

    // 7. Read a message from |mp2|.
    EXPECT_EQ("baz", ReadMessage(mp2));

    // 8. Close |mp0|.
    ASSERT_EQ(MOJO_RESULT_OK, MojoClose(mp0));

    // 9. Tell the client to quit.
    WriteMessage(server_mp, "quit");

    // 10. Wait on |mp2| (which should eventually fail) and then close it.
    MojoHandleSignalsState state;
    ASSERT_EQ(MOJO_RESULT_FAILED_PRECONDITION,
              MojoWait(mp2, MOJO_HANDLE_SIGNAL_READABLE,
                       MOJO_DEADLINE_INDEFINITE,
                       &state));
    ASSERT_EQ(MOJO_HANDLE_SIGNAL_PEER_CLOSED, state.satisfied_signals);
    ASSERT_EQ(MOJO_HANDLE_SIGNAL_PEER_CLOSED, state.satisfiable_signals);

    ASSERT_EQ(MOJO_RESULT_OK, MojoClose(mp2));
  END_CHILD()
}

DEFINE_TEST_CLIENT_TEST_WITH_PIPE(MultiprocessChannelsClient, EmbedderTest,
                                  client_mp) {
  // 1. Read the first message from |client_mp|.
  EXPECT_EQ("hello", ReadMessage(client_mp));

  // 2. Write a message to |client_mp| (attaching nothing).
  WriteMessage(client_mp, "world!");

  // 4. Read a message from |client_mp|, which should have |mp1| attached.
  MojoHandle mp1;
  EXPECT_EQ("Bar", ReadMessageWithHandles(client_mp, &mp1, 1));

  // 5. Create a new message pipe (endpoints |mp2| and |mp3|).
  MojoHandle mp2, mp3;
  CreateMessagePipe(&mp2, &mp3);

  // 6. Write a message to |mp3|.
  WriteMessage(mp3, "baz");

  // 7. Close |mp3|.
  ASSERT_EQ(MOJO_RESULT_OK, MojoClose(mp3));

  // 8. Write a message to |mp1|, attaching |mp2|.
  WriteMessageWithHandles(mp1, "quux", &mp2, 1);
  mp2 = MOJO_HANDLE_INVALID;

  // 9. Read a message from |mp1|.
  EXPECT_EQ("FOO", ReadMessage(mp1));

  EXPECT_EQ("quit", ReadMessage(client_mp));

  // 10. Wait on |mp1| (which should eventually fail) and then close it.
  MojoHandleSignalsState state;
  ASSERT_EQ(MOJO_RESULT_FAILED_PRECONDITION,
            MojoWait(mp1, MOJO_HANDLE_SIGNAL_READABLE,
                      MOJO_DEADLINE_INDEFINITE, &state));
  ASSERT_EQ(MOJO_HANDLE_SIGNAL_PEER_CLOSED, state.satisfied_signals);
  ASSERT_EQ(MOJO_HANDLE_SIGNAL_PEER_CLOSED, state.satisfiable_signals);
  ASSERT_EQ(MOJO_RESULT_OK, MojoClose(mp1));
}

TEST_F(EmbedderTest, MultiprocessBaseSharedMemory) {
  RUN_CHILD_ON_PIPE(MultiprocessSharedMemoryClient, server_mp)
    // 1. Create a base::SharedMemory object and create a mojo shared buffer
    // from it.
    base::SharedMemoryCreateOptions options;
    options.size = 123;
    base::SharedMemory shared_memory;
    ASSERT_TRUE(shared_memory.Create(options));
    base::SharedMemoryHandle shm_handle = base::SharedMemory::DuplicateHandle(
        shared_memory.handle());
    MojoHandle sb1;
    ASSERT_EQ(MOJO_RESULT_OK,
              CreateSharedBufferWrapper(shm_handle, 123, false, &sb1));

    // 2. Map |sb1| and write something into it.
    char* buffer = nullptr;
    ASSERT_EQ(MOJO_RESULT_OK,
              MojoMapBuffer(sb1, 0, 123, reinterpret_cast<void**>(&buffer), 0));
    ASSERT_TRUE(buffer);
    memcpy(buffer, kHelloWorld, sizeof(kHelloWorld));

    // 3. Duplicate |sb1| into |sb2| and pass to |server_mp|.
    MojoHandle sb2 = MOJO_HANDLE_INVALID;
    EXPECT_EQ(MOJO_RESULT_OK, MojoDuplicateBufferHandle(sb1, 0, &sb2));
    EXPECT_NE(MOJO_HANDLE_INVALID, sb2);
    WriteMessageWithHandles(server_mp, "hello", &sb2, 1);

    // 4. Read a message from |server_mp|.
    EXPECT_EQ("bye", ReadMessage(server_mp));

    // 5. Expect that the contents of the shared buffer have changed.
    EXPECT_EQ(kByeWorld, std::string(buffer));

    // 6. Map the original base::SharedMemory and expect it contains the
    // expected value.
    ASSERT_TRUE(shared_memory.Map(123));
    EXPECT_EQ(kByeWorld,
              std::string(static_cast<char*>(shared_memory.memory())));

    ASSERT_EQ(MOJO_RESULT_OK, MojoClose(sb1));
  END_CHILD()
}

DEFINE_TEST_CLIENT_TEST_WITH_PIPE(MultiprocessSharedMemoryClient, EmbedderTest,
                                  client_mp) {
  // 1. Read the first message from |client_mp|, which should have |sb1| which
  // should be a shared buffer handle.
  MojoHandle sb1;
  EXPECT_EQ("hello", ReadMessageWithHandles(client_mp, &sb1, 1));

  // 2. Map |sb1|.
  char* buffer = nullptr;
  ASSERT_EQ(MOJO_RESULT_OK,
            MojoMapBuffer(sb1, 0, 123, reinterpret_cast<void**>(&buffer), 0));
  ASSERT_TRUE(buffer);

  // 3. Ensure |buffer| contains the values we expect.
  EXPECT_EQ(kHelloWorld, std::string(buffer));

  // 4. Write into |buffer| and send a message back.
  memcpy(buffer, kByeWorld, sizeof(kByeWorld));
  WriteMessage(client_mp, "bye");

  // 5. Extract the shared memory handle and ensure we can map it and read the
  // contents.
  base::SharedMemoryHandle shm_handle;
  ASSERT_EQ(MOJO_RESULT_OK,
            PassSharedMemoryHandle(sb1, &shm_handle, nullptr, nullptr));
  base::SharedMemory shared_memory(shm_handle, false);
  ASSERT_TRUE(shared_memory.Map(123));
  EXPECT_NE(buffer, shared_memory.memory());
  EXPECT_EQ(kByeWorld, std::string(static_cast<char*>(shared_memory.memory())));

  // 6. Close |sb1|. Should fail because |PassSharedMemoryHandle()| should have
  // closed the handle.
  EXPECT_EQ(MOJO_RESULT_INVALID_ARGUMENT, MojoClose(sb1));
}

#if defined(OS_MACOSX) && !defined(OS_IOS)
TEST_F(EmbedderTest, MultiprocessMachSharedMemory) {
  RUN_CHILD_ON_PIPE(MultiprocessSharedMemoryClient, server_mp)
    // 1. Create a Mach base::SharedMemory object and create a mojo shared
    // buffer from it.
    base::SharedMemoryCreateOptions options;
    options.size = 123;
    base::SharedMemory shared_memory;
    ASSERT_TRUE(shared_memory.Create(options));
    base::SharedMemoryHandle shm_handle = base::SharedMemory::DuplicateHandle(
        shared_memory.handle());
    MojoHandle sb1;
    ASSERT_EQ(MOJO_RESULT_OK,
              CreateSharedBufferWrapper(shm_handle, 123, false, &sb1));

    // 2. Map |sb1| and write something into it.
    char* buffer = nullptr;
    ASSERT_EQ(MOJO_RESULT_OK,
              MojoMapBuffer(sb1, 0, 123, reinterpret_cast<void**>(&buffer), 0));
    ASSERT_TRUE(buffer);
    memcpy(buffer, kHelloWorld, sizeof(kHelloWorld));

    // 3. Duplicate |sb1| into |sb2| and pass to |server_mp|.
    MojoHandle sb2 = MOJO_HANDLE_INVALID;
    EXPECT_EQ(MOJO_RESULT_OK, MojoDuplicateBufferHandle(sb1, 0, &sb2));
    EXPECT_NE(MOJO_HANDLE_INVALID, sb2);
    WriteMessageWithHandles(server_mp, "hello", &sb2, 1);

    // 4. Read a message from |server_mp|.
    EXPECT_EQ("bye", ReadMessage(server_mp));

    // 5. Expect that the contents of the shared buffer have changed.
    EXPECT_EQ(kByeWorld, std::string(buffer));

    // 6. Map the original base::SharedMemory and expect it contains the
    // expected value.
    ASSERT_TRUE(shared_memory.Map(123));
    EXPECT_EQ(kByeWorld,
              std::string(static_cast<char*>(shared_memory.memory())));

    ASSERT_EQ(MOJO_RESULT_OK, MojoClose(sb1));
  END_CHILD()
}

enum class HandleType {
  POSIX,
  MACH,
  MACH_NULL,
};

const HandleType kTestHandleTypes[] = {
  HandleType::MACH,
  HandleType::MACH_NULL,
  HandleType::POSIX,
  HandleType::POSIX,
  HandleType::MACH,
};

// Test that we can mix file descriptors and mach port handles.
TEST_F(EmbedderTest, MultiprocessMixMachAndFds) {
  const size_t kShmSize = 1234;
  RUN_CHILD_ON_PIPE(MultiprocessMixMachAndFdsClient, server_mp)
    // 1. Create fds or Mach objects and mojo handles from them.
    MojoHandle platform_handles[arraysize(kTestHandleTypes)];
    for (size_t i = 0; i < arraysize(kTestHandleTypes); i++) {
      const auto type = kTestHandleTypes[i];
      ScopedPlatformHandle scoped_handle;
      if (type == HandleType::POSIX) {
        // The easiest source of fds is opening /dev/null.
        base::File file(base::FilePath("/dev/null"),
                        base::File::FLAG_OPEN | base::File::FLAG_WRITE);
        ASSERT_TRUE(file.IsValid());
        scoped_handle.reset(PlatformHandle(file.TakePlatformFile()));
        EXPECT_EQ(PlatformHandle::Type::POSIX, scoped_handle.get().type);
      } else if (type == HandleType::MACH_NULL) {
        scoped_handle.reset(PlatformHandle(
            static_cast<mach_port_t>(MACH_PORT_NULL)));
        EXPECT_EQ(PlatformHandle::Type::MACH, scoped_handle.get().type);
      } else {
        base::SharedMemoryCreateOptions options;
        options.size = kShmSize;
        base::SharedMemory shared_memory;
        ASSERT_TRUE(shared_memory.Create(options));
        base::SharedMemoryHandle shm_handle =
            base::SharedMemory::DuplicateHandle(shared_memory.handle());
        scoped_handle.reset(PlatformHandle(shm_handle.GetMemoryObject()));
        EXPECT_EQ(PlatformHandle::Type::MACH, scoped_handle.get().type);
      }
      ASSERT_EQ(MOJO_RESULT_OK, CreatePlatformHandleWrapper(
          std::move(scoped_handle), platform_handles + i));
    }

    // 2. Send all the handles to the child.
    WriteMessageWithHandles(server_mp, "hello", platform_handles,
                            arraysize(kTestHandleTypes));

    // 3. Read a message from |server_mp|.
    EXPECT_EQ("bye", ReadMessage(server_mp));
  END_CHILD()
}

DEFINE_TEST_CLIENT_TEST_WITH_PIPE(MultiprocessMixMachAndFdsClient, EmbedderTest,
                                  client_mp) {
  const int kNumHandles = arraysize(kTestHandleTypes);
  MojoHandle platform_handles[kNumHandles];

  // 1. Read from |client_mp|, which should have a message containing
  // |kNumHandles| handles.
  EXPECT_EQ("hello",
            ReadMessageWithHandles(client_mp, platform_handles, kNumHandles));

  // 2. Extract each handle, and verify the type.
  for (int i = 0; i < kNumHandles; i++) {
    const auto type = kTestHandleTypes[i];
    ScopedPlatformHandle scoped_handle;
    ASSERT_EQ(MOJO_RESULT_OK,
              PassWrappedPlatformHandle(platform_handles[i], &scoped_handle));
    if (type == HandleType::POSIX) {
      EXPECT_NE(0, scoped_handle.get().handle);
      EXPECT_EQ(PlatformHandle::Type::POSIX, scoped_handle.get().type);
    } else if (type == HandleType::MACH_NULL) {
      EXPECT_EQ(static_cast<mach_port_t>(MACH_PORT_NULL),
                scoped_handle.get().port);
      EXPECT_EQ(PlatformHandle::Type::MACH, scoped_handle.get().type);
    } else {
      EXPECT_NE(static_cast<mach_port_t>(MACH_PORT_NULL),
                scoped_handle.get().port);
      EXPECT_EQ(PlatformHandle::Type::MACH, scoped_handle.get().type);
    }
  }

  // 3. Say bye!
  WriteMessage(client_mp, "bye");
}

#endif  // defined(OS_MACOSX) && !defined(OS_IOS)

// TODO(vtl): Test immediate write & close.
// TODO(vtl): Test broken-connection cases.

#endif  // !defined(OS_IOS)

NamedPlatformHandle GenerateChannelName() {
#if defined(OS_POSIX)
  base::FilePath temp_dir;
  CHECK(base::PathService::Get(base::DIR_TEMP, &temp_dir));
  return NamedPlatformHandle(
      temp_dir.AppendASCII(GenerateRandomToken()).value());
#else
  return NamedPlatformHandle(GenerateRandomToken());
#endif
}

void CreateClientHandleOnIoThread(const NamedPlatformHandle& named_handle,
                                  ScopedPlatformHandle* output) {
  *output = CreateClientHandle(named_handle);
}

TEST_F(EmbedderTest, ClosePendingPeerConnection) {
  NamedPlatformHandle named_handle = GenerateChannelName();
  std::string peer_token = GenerateRandomToken();
  ScopedMessagePipeHandle server_pipe =
      ConnectToPeerProcess(CreateServerHandle(named_handle), peer_token);
  ClosePeerConnection(peer_token);
  EXPECT_EQ(MOJO_RESULT_OK,
            Wait(server_pipe.get(), MOJO_HANDLE_SIGNAL_PEER_CLOSED,
                 MOJO_DEADLINE_INDEFINITE, nullptr));
  base::MessageLoop message_loop;
  base::RunLoop run_loop;
  ScopedPlatformHandle client_handle;
  // Closing the channel involves posting a task to the IO thread to do the
  // work. By the time the local message pipe has been observerd as closed,
  // that task will have been posted. Therefore, a task to create the client
  // connection should be handled after the channel is closed.
  GetIOTaskRunner()->PostTaskAndReply(
      FROM_HERE,
      base::Bind(&CreateClientHandleOnIoThread, named_handle, &client_handle),
      run_loop.QuitClosure());
  run_loop.Run();
  EXPECT_FALSE(client_handle.is_valid());
}

#if !defined(OS_IOS)

TEST_F(EmbedderTest, ClosePipeToConnectedPeer) {
  set_launch_type(LaunchType::PEER);
  auto& controller = StartClient("ClosePipeToConnectedPeerClient");
  MojoHandle server_mp = controller.pipe();
  // 1. Write a message to |server_mp| (attaching nothing).
  WriteMessage(server_mp, "hello");

  // 2. Read a message from |server_mp|.
  EXPECT_EQ("world!", ReadMessage(server_mp));

  controller.ClosePeerConnection();

  EXPECT_EQ(MOJO_RESULT_OK, MojoWait(server_mp, MOJO_HANDLE_SIGNAL_PEER_CLOSED,
                                     MOJO_DEADLINE_INDEFINITE, nullptr));

  EXPECT_EQ(0, controller.WaitForShutdown());
}

DEFINE_TEST_CLIENT_TEST_WITH_PIPE(ClosePipeToConnectedPeerClient, EmbedderTest,
                                  client_mp) {
  // 1. Read the first message from |client_mp|.
  EXPECT_EQ("hello", ReadMessage(client_mp));

  // 2. Write a message to |client_mp| (attaching nothing).
  WriteMessage(client_mp, "world!");

  ASSERT_EQ(MOJO_RESULT_OK,
            MojoWait(client_mp, MOJO_HANDLE_SIGNAL_PEER_CLOSED,
                      MOJO_DEADLINE_INDEFINITE, nullptr));
}

TEST_F(EmbedderTest, ClosePipeToConnectingPeer) {
  set_launch_type(LaunchType::PEER);
  auto& controller = StartClient("ClosePipeToConnectingPeerClient");
  controller.ClosePeerConnection();

  MojoHandle server_mp = controller.pipe();

  EXPECT_EQ(MOJO_RESULT_OK, MojoWait(server_mp, MOJO_HANDLE_SIGNAL_PEER_CLOSED,
                                     MOJO_DEADLINE_INDEFINITE, nullptr));

  EXPECT_EQ(0, controller.WaitForShutdown());
}

DEFINE_TEST_CLIENT_TEST_WITH_PIPE(ClosePipeToConnectingPeerClient, EmbedderTest,
                                  client_mp) {
  ASSERT_EQ(MOJO_RESULT_OK, MojoWait(client_mp, MOJO_HANDLE_SIGNAL_PEER_CLOSED,
                                     MOJO_DEADLINE_INDEFINITE, nullptr));
}

#endif  // !defined(OS_IOS)

}  // namespace
}  // namespace edk
}  // namespace mojo
