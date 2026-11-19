# beseda

Peer-to-peer UDP console chat in C++17.

## Build

```sh
cmake -S . -B build && cmake --build build
```

## Usage

```
beseda [-p LISTEN_PORT] [HOST[:PORT]]
```

Both ports default to 50000. Without `HOST`, it replies to whoever messages it first.
Type a line and press Enter to send it; Ctrl-D quits.

```sh
./build/beseda -p 50000 127.0.0.1:50001   # terminal 1
./build/beseda -p 50001 127.0.0.1:50000   # terminal 2
```
