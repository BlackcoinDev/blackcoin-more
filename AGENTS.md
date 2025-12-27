# Agent Coding Guidelines for Blackcoin More

This document provides essential information for agentic coding agents working in the Blackcoin More repository.

## Build System

**Build System**: Autotools (GNU Autoconf/Automake)
**Single Source Configuration**: `./configure`
**Recursive Build**: `make -j$(nproc)`

### Essential Build Commands
```bash
# Initial setup
./autogen.sh                    # Generate configure script
./configure --disable-tests    # Minimal build
./configure --enable-tests     # Build with tests
make                           # Build all targets
make install                   # Install to system

# Specific targets
make blackmored                # Build daemon only
make blackmore-cli             # Build CLI only  
make blackmore-qt              # Build GUI only
make check                     # Run all tests
make dist                      # Create source distribution
```

### Dependencies
- **Required**: libssl-dev, libevent-dev, libboost-dev, libsodium-dev
- **Wallet**: libdb-dev, libsqlite3-dev
- **GUI**: qtbase5-dev, qttools5-dev
- **Tests**: python3, bash, docker

## Testing

### Running Tests
```bash
# All tests
make check

# C++ unit tests only
src/test/test_bitcoin

# Python functional tests
test/functional/test_runner.py

# Single test
test/functional/test_runner.py wallet_upgradewallet.py

# Debug single test
test/functional/test_runner.py --verbose wallet_upgradewallet.py

# CI test environment locally
env -i HOME="$HOME" PATH="$PATH" USER="$USER" bash -c 'FILE_ENV="./ci/test/00_setup_env_native_asan.sh" ./ci/test_run_all.sh'
```

### Test Categories
- **Unit Tests**: C++ tests in `src/test/`
- **Functional Tests**: Python tests in `test/functional/`
- **Fuzzing**: `./src/test/fuzz/fuzz_bitcoin`
- **Benchmarks**: `./src/bench/bench_bitcoin`

## Code Style

### C++ Guidelines
**Style**: Bitcoin Core style (clang-format enforced)
**Standard**: C++17
**Linting**: `./ci/lint/06_script.sh`

**Key Conventions**:
- **Files**: Use `.cpp` for implementation, `.h` for headers
- **Naming**: 
  - Classes: `PascalCase` (`CBlock`, `CWallet`)
  - Functions: `PascalCase` (`GetBlockHash()`, `CreateTransaction()`)
  - Variables: `snake_case` (`block_height`, `transaction_fee`)
  - Constants: `UPPER_CASE` (`MAX_BLOCK_SIZE`)
- **Headers**: Order: standard library, project headers, third-party
- **Includes**: Use quotes for local headers, angle brackets for system libraries
- **Memory**: Use RAII, smart pointers (`std::unique_ptr`, `std::shared_ptr`)
- **Error Handling**: Use exceptions for exceptional cases, return values for expected errors

### Python Guidelines  
**Style**: PEP 8
**Version**: Python 3.6+
**Testing**: Use `unittest` framework

**Key Conventions**:
- **Files**: Use lowercase with underscores (`test_runner.py`)
- **Naming**: 
  - Classes: `PascalCase`
  - Functions/variables: `snake_case`
  - Constants: `UPPER_CASE`
- **Imports**: Group by standard library, third-party, local

### Documentation
- **C++**: Doxygen-style comments for public APIs
- **Python**: Docstrings for all public functions/classes
- **Commit Messages**: Follow conventional commits format

## Linting and Quality

### Lint Commands
```bash
# Run all linting
./ci/lint/06_script.sh

# Individual lint checks
test/lint/all-lint.py
test/lint/check-doc.py
test/lint/commit-script-check.sh
```

### Code Quality Tools
- **Clang Format**: Code formatting (`.clang-format`)
- **Clang Tidy**: Static analysis
- **Cppcheck**: Static analysis  
- **Sanitizers**: ASan, UBSan, TSan (in CI)

## Project Structure

### Key Directories
- **src/**: C++ source code
- **src/test/**: C++ unit tests
- **src/bench/**: Performance benchmarks
- **src/qt/**: Qt GUI application
- **test/functional/**: Python integration tests
- **ci/**: Continuous integration scripts
- **doc/**: Documentation
- **contrib/**: External contributions and tools

### Core Components
- **Consensus**: `src/consensus/` - Protocol validation
- **Validation**: `src/validation.cpp` - Block/transaction validation
- **Mempool**: `src/txmempool.cpp` - Transaction pool
- **P2P**: `src/net/` - Peer-to-peer networking
- **Wallet**: `src/wallet/` - Wallet functionality
- **RPC**: `src/rpc/` - Remote procedure calls

## Development Workflow

### Testing Changes
```bash
# After any change
make check                    # Run all tests
./ci/lint/06_script.sh       # Run linting
make clean && make           # Clean rebuild to catch issues

# For specific areas
make -j$(nproc) check TESTS="test_bitcoin"  # Unit tests only
```

### Build Debug Versions
```bash
# Debug build
./configure CFLAGS="-g -O0" --enable-debug
make

# Sanitizer builds
./configure --with-sanitizers=address,undefined
make

# Profiling build  
./configure --enable-gprof
make
```

## Repository Rules

### Don't Modify
- **src/secp256k1/**: External library (subtree)
- **src/leveldb/**: External library (subtree)
- **src/minisketch/**: External library (subtree)
- **depends/**: Build dependencies (auto-generated)

### Configuration Files
- **.clang-format**: C++ formatting rules
- **.gitignore**: Git ignore patterns
- **configure.ac**: Build configuration
- **Makefile.am**: Build rules

## Common Agent Tasks

### Adding New RPC Methods
1. Declare in `src/rpc/` header files
2. Implement in corresponding `.cpp` files
3. Add tests in `test/functional/`
4. Update documentation in `doc/`

### Adding Tests
1. **Unit Tests**: Add to `src/test/` directory
2. **Functional Tests**: Add Python test to `test/functional/`
3. **Benchmark Tests**: Add to `src/bench/`

### Modifying Consensus
1. Check consensus rules in `src/consensus/`
2. Update validation in `src/validation.cpp`
3. Add comprehensive tests
4. Consider network upgrade implications

## Emergency Procedures

### Reverting Changes
```bash
git reset --hard HEAD~1    # Last commit
git stash                 # Stash uncommitted changes
git checkout -- .         # Discard local changes
```

### Build Issues
```bash
make clean && make distclean
./autogen.sh && ./configure && make
```

### Test Failures
```bash
# Run specific failing test with verbose output
test/functional/test_runner.py --verbose <test_name>

# Debug with gdb
gdb --args src/test/test_bitcoin
```

## Contact and Resources

- **Repository**: https://github.com/CoinBlack/blackcoin-more
- **Documentation**: `doc/` directory
- **Issue Tracker**: GitHub Issues
- **Contributing**: Follow standard GitHub workflow (fork, branch, PR)

---
*Last Updated: December 27, 2025*