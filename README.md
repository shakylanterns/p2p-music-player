# CSCI3280 Project

## How do the Client classes work???

The `BaseClient` constructor does a few things:

1. Initialize the acceptor (which accept connections) with that computers IP address and a designated port
2. Start the acceptor
3. `trap_signal` is supposed to trap SIGINT and SIGTERM, now it does nothing
4. `accept_socket()` starts accepting connections
5. start the repeating timer (see below)
6. start the IO context in a new thread

`accept_socket` does these things:

1. It calls `async_accept`, which asynchronously accepts connections. It
   receives a "token" (from the docs), which is a complicated name for
   "callback". Asynchronous means that this function will probably be called
   AFTER `accept_socket` pops off the stack. (Magic)

2. When it receives a connection, the token gives us a socket. Add that socket
   to the `peers` array. (so we can use it later)

3. Call `start_reading()` so we can start reading from that socket
4. INSIDE the callback, we call `accept_socket` again!! (this is NOT a recursion
   since it is asynchronous!!).

`connect_to_peer` does the opposite of `accept_socket`, it tries to connect to
others instead of others connecting to them.

1. Make a socket and add it to the peers list
2. Build a series of endpoints (the same host and service may correspond to
   multiple endpoints??)
3. Call `async_connect` with the endpoints
4. If none of the endpoints work, remove that socket
5. If it works, then it works.

`start_reading`:

1. It calls `async_read`, which tries to wait for something to read
2. First the header of the message is read. The header only contains what type
   of message it is (see `message-type.h`), and the size of body.
3. If it doesn't have a body, add the message to the `in_msgs` queue (see below
   on when this queue is read). Call `start_reading` again (this is NOT a
   recursion, again!)
4. If it has a body, call `read_body`. After that, push the message into the
   `in_msgs` queue. Call `start_reading` again.

`start_writing`:

It first checks if there are any messages in `out_msgs`, if not it quits.

1. It calls `async_write`, which tries to write to the socket
2. First we write the header
3. If body is empty, we quit (this time don't need to call `async_write`
   again, see `cycle` below)
4. If there is a body, call `write_body`
5. `write_body` will write the bytes to the message body, then quit.

`cycle`:

The timer is `async_timer`, which runs the "token" (callback) every N seconds,
now the interval is one second.

For each cycle, do the following:

1. Handle all messages in the `in_msgs` queue.
2. Calls `start_writing`

I don't know if this design is good or not, now each message will have a delay
of at least one second, but this is easiest I can think of.

## What is a Message?

A message contains a header and body.

Header has two fields:

1. `MessageType`: type of message
2. `std::size_t`: size of body (zero if body is empty)

Body is just a vector of char.

The push something into the message body, use `<<`. To pull something out of the
message body, use `>>`. Note that the pushing is only a **shallow copy**. For
arrays and strings, and other composite types, a custom operator overloading has
to be provided for it to work correctly. See the `message.h` file for examples.

The message that is sent in the client is actually a `MessageWithOwner`. It is
nothing but message with a peer ID. Peer ID is a cleaner way to identify a peer
without using host, port and so on.

## So what is the difference between `BaseClient` and `Client`?

`BaseClient` dives into the details of reading and sending messages,
establishing connections etc. For handling messages, please use the
`handle_message` function in `Client`.

This `handle_message` is called for each message in `in_msgs`. The argument
passed in is `MessageWithOwner` so that the handler can see the client ID.

This handler can do whatever it wants, but most commonly it will scrutinize the
incoming message and construct a response. The response can be saved to the
`out_msgs` array by `push_message`. Note that the message will not be sent
immediately, it will be sent in the next cycle.


## Steps for Compilation

In this project there is a `CMakeLists.txt` which orchestrates the compilation.
Before you generate the build files, please make sure the following libraries
are present:

- `gtkmm3.0`: GTK+ bindings for C++, version 3. Any minor version should work.
- `gstreamer`, `gstreamer-app`, `gstreamer-video`, `gstreamer-audio`:
  `gstreamer` library with plugins. The requirements of version are in the
  `CMakeLists.txt` file.
- `taglib`: handling audio metadata
- `sqlite3`: for file database
- `SQLiteCpp`: C++ wrapper for sqlite3
- `asio`: For asynchronous TCP/IP connections
- `googletest`: for testing

`googletest` and `SQLiteCpp` will be pulled from their
GitHub repository automatically. There is no need to install that library
beforehand.

1. Clone the repository

```sh
git clone https://github.com/1155158936/csci3280-project.git
```

2. Make sure you are at the **root** of the repository. The configuration for
   building the files can be generated by the following command:

```sh
cmake -B build -S src -DCMAKE_EXPORT_COMPILE_COMMANDS=1
```

- `-B` is for the directory where the executables, cache files etc will live.
- `-S` is the source directory where `CMakeLists.txt` and other source files will live.
- `-D` defines variables. `CMAKE_EXPORT_COMPILE_COMMANDS` specifies that a
`compile_commands.json` should be generated so that language servers how to find
the library headers. This is not required if you don't have these services in
the text editor.

This command only needs to run after `CMakeLists.txt` is changed (to regenerate
the build configuration files).

3. Compile the files with

```sh
cmake --build build [--config <Debug or Release>]
```

This builds the project. The `--config` flag may be needed for some build
systems (Visual Studio?).

4. Running the program

```sh
./build/<name of executable>
```

CMake places the executable in the `build` folder.

## Development

1. Including Libraries

Use `PkgConfig` magic to find the library files and headers, like below for `gtkmm-3.0`.

```cmake
# magic
find_package(PkgConfig)

pkg_check_modules(gtk3 REQUIRED IMPORTED_TARGET gtkmm-3.0)

target_link_libraries(p2pmss PRIVATE PkgConfig::gtk3)

```
## Testing

If you have too much time, you can write some tests for it. Currently, I have
set up google test (it is open source so no worries) for this project. It should
be automatically downloaded the first time when you generate the build config.

To add a test, create a new file in `src/tests/` with name starting with
`test_*.cpp`. Inside the file you define tests. One test is one suite.

Each test looks like this:

```cpp
TEST(TestSuiteName, TestName) {
  // assertions and C++ code
}
```

Please read [their testing
primer](https://google.github.io/googletest/primer.html) for more details.

I made a function in `CMakeLists.txt` so that tests can be added easily:

```cmake
add_test(targetName <...needed sources>)
```

The executable is just like any other thing you build but linked with google
tests. `gtest_discover_tests` is required for `ctest` to acknowledge its
existence.

Build the project as usual, and run:

```sh
ctest --test-dir build --output-on-failure
```

Or you can `cd` into the build directory first and run `ctest`.

More information is on [their docs on
CMake](https://google.github.io/googletest/quickstart-cmake.html) and [on this
website](https://cmake.org/cmake/help/git-stage/module/GoogleTest.html).

## Version Control

1. Before you commit the changes, add it to the staging area first. Git will
   only commit things that are in the staging area.

```
git add <...files>
```

Use `.` for `<...files>` if you want to stage all changes in the working directory.

2. Commit

```
git commit
```

Git will use the editor specified in `$EDITOR` environment variable. If it is
empty `vim` will be used. Write the commit message, then save and quit.

By the way, don't commit build artifacts (anything that is produced by CMake)!!

3. Push to a remote repository

```
git push origin master
```

`origin` should point to the URL you used to clone this repository. If not add:

```
git remote add origin https://github.com/1155158936/csci3280-project
```

You can also push to another branch if you don't want to mess up the `master` branch.

4. Fetching remote changes

```
git fetch
```

This will sync the local database with the remote one, although it doesn't apply
the changes to the working directory.

```
git pull
```

I would recommend you committing the changes in your working directory before
pulling anything new.

## Formatting

I recommend following Antony's style in `lrc.h` and `lrc.cpp`, which is **four
spaces**.
