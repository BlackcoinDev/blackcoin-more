# Agent Coding Guidelines for Blackcoin More

## Build System

**Build System**: Autotools (GNU Autoconf/Automake)
```bash
./autogen.sh
./configure --enable-tests
make -j$(nproc)

# Specific targets
make blackmored              # Build daemon
make blackmore-cli           # Build CLI
make blackmore-qt            # Build GUI
make test_blackmore          # Build test binary
make check                   # Run all tests
```

### Dependencies
- **Required**: libssl-dev, libevent-dev, libboost-dev, libsodium-dev
- **Wallet**: libdb-dev, libsqlite3-dev
- **GUI**: qtbase5-dev, qttools5-dev

## Testing

```bash
make check                           # All tests
src/test/test_blackmore              # C++ unit tests
test/functional/test_runner.py       # Python functional tests
test/functional/test_runner.py wallet_upgradewallet.py  # Single test
test/functional/test_runner.py --verbose wallet_upgradewallet.py  # Verbose
test/functional/test_runner.py --cached wallet_*.py  # Category
test/functional/test_runner.py --verbose --failfast <test_name>  # Debug
```

## Code Style

### C++ Guidelines
**Standard**: C++17 | **Formatter**: `clang-format` (config in `src/.clang-format`)

**Files**: `.cpp` for implementation, `.h` for headers
**Imports**: Group by standard library → project headers → third-party

**Naming**:
- Classes: `PascalCase` (`CBlock`, `CWallet`)
- Functions: `PascalCase` (`GetBlockHash()`, `CreateTransaction()`)
- Variables: `snake_case` (`block_height`, `transaction_fee`)
- Constants: `UPPER_CASE` (`MAX_BLOCK_SIZE`)

**Style**:
- RAII, smart pointers (`std::unique_ptr`, `std::shared_ptr`)
- Pointer alignment left (`Type* var`)
- Exceptions for exceptional cases, `std::optional`/`Result` for expected errors
- Column limit: 0 (no enforced limit)

### Python Guidelines  
**Style**: PEP 8 | **Version**: Python 3.6+ | **Testing**: `unittest` framework

**Naming**:
- Classes: `PascalCase`
- Functions/variables: `snake_case`
- Constants: `UPPER_CASE`

## Linting

```bash
./ci/lint/06_script.sh          # All linting
test/lint/all-lint.py           # Code style, formatting
test/lint/check-doc.py          # Documentation
```

**Tools**: Clang Format, Clang Tidy, Cppcheck, ASan/UBSan/TSan

## Project Structure

**Key Directories**:
- `src/`: C++ source code
- `src/test/`: C++ unit tests
- `src/bench/`: Performance benchmarks
- `src/qt/`: Qt GUI
- `test/functional/`: Python integration tests
- `ci/`: CI scripts

**Core Components**:
- `src/consensus/`: Protocol validation
- `src/validation.cpp`: Block/transaction validation
- `src/txmempool.cpp`: Transaction pool
- `src/net/`: Peer-to-peer networking
- `src/wallet/`: Wallet functionality
- `src/rpc/`: Remote procedure calls

## Development Workflow

```bash
make check                    # Run all tests
./ci/lint/06_script.sh       # Run linting
make clean && make           # Clean rebuild

./configure CFLAGS="-g -O0" --enable-debug  # Debug build
```

## Repository Rules

### Don't Modify
- `src/secp256k1/`, `src/leveldb/`, `src/minisketch/`, `src/crc32c/`: External libraries (subtree)
- `depends/`: Build dependencies (auto-generated)

### Configuration Files
- `src/.clang-format`: C++ formatting rules
- `src/.clang-tidy`: Static analysis rules
- `.editorconfig`: Basic formatting rules
- `configure.ac`: Build configuration

## Common Agent Tasks

### Adding New RPC Methods
1. Declare in `src/rpc/` header files
2. Implement in corresponding `.cpp` files
3. Add tests in `test/functional/`
4. Update documentation in `doc/JSON-RPC-interface.md`

### Adding Tests
- **Unit Tests**: Add to `src/test/`
- **Functional Tests**: Add to `test/functional/`
- **Benchmark Tests**: Add to `src/bench/`

### Modifying Consensus
1. Check `src/consensus/` for consensus rules
2. Update `src/validation.cpp`
3. Add comprehensive tests
4. Consider network upgrade implications

## Emergency Procedures

```bash
git reset --hard HEAD~1       # Revert last commit
git stash                     # Stash uncommitted changes
git checkout -- .             # Discard local changes

make clean && make distclean  # Build issues
./autogen.sh && ./configure && make

test/functional/test_runner.py --verbose --failfast <test_name>  # Debug test
gdb --args src/test/test_blackmore
```

---
*Last Updated: January 19, 2026*
