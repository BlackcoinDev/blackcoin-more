# Wallet Module — Agent Knowledge Base

**Scope**: `src/wallet/` | **Score**: 22 (45 files, BDB/SQLite, financial critical)

---

## ⚠️ CRITICAL: Bitcoin 26.x → 30.x Upgrade Context

### Never-Port Features (Wallet Module)

| Bitcoin Core Feature | Blackcoin More Status | Action |
|---------------------|----------------------|--------|
| BDB wallet removal | **BDB 6.2 REQUIRED** | Never remove BDB code |
| SQLite-only wallets | BDB supported | Never force migration |
| Descriptor wallet migration | Optional | Never force `migratewallet` |
| RBF wallet signaling | Completely disabled | Never enable RBF |

### ⚠️ CRITICAL: BDB 6.2 Preservation

**Bitcoin Core 30.x removed BDB wallet support. Blackcoin More MUST preserve BDB 6.2.**

```cpp
// src/wallet/db.h - MUST PRESERVE
#if defined(USE_BDB)
#include <db_cxx.h>
#endif  // Never remove this conditional

// src/Makefile.am - Never remove BDB sources
src_wallet_SOURCES += db.cpp db.h bdb.cpp bdb.h  // PRESERVE THIS
```

**⚠️ WARNING**: BDB 6.2 wallet files are NOT compatible with newer versions. Users must be able to open existing wallets.

### Wallet Database Support

**Current State**:
- ✅ BDB 6.2: **REQUIRED** (read/write support for existing wallets)
- ✅ SQLite: Supported (for new wallets)
- ❌ BDB creation: **DEPRECATED** (new wallets use SQLite)

**Migration Path**:
- Users CAN migrate BDB → SQLite via `migratewallet` RPC
- Migration is OPTIONAL, not required
- Existing BDB wallets continue to work

### Fee Structure (Static)

```cpp
// src/wallet/wallet.cpp
static const CAmount MIN_FEE = 100000;  // Fixed, not dynamic
```

**Implications**:
- No dynamic fee estimation in wallet
- Fixed 100,000 sat/kvB minimum
- No RBF fee bumping capability

### RBF Disabled (Wallet Level)

```cpp
// src/wallet/rpc/wallet.cpp
// RBF is completely disabled - no opt_in_rbf option
// First-seen rule remains active for double-spend protection
```

### Anti-Patterns (THIS MODULE)

- **NEVER**: Create new BDB wallets (deprecated, but READ support MUST work)
- **NEVER**: Use `accounts` API (replaced by labels)
- **NEVER**: Skip passphrase backup warnings
- **NEVER**: Import untrusted metadata/files
- **NEVER**: Remove BDB wallet code (READ support REQUIRED)
- **NEVER**: Force wallet migration (must be user-initiated)
- **NEVER**: Enable RBF wallet signaling

---

## Overview

Wallet storage and management layer. Handles key storage, transaction creation, BDB/SQLite persistence, coin selection. Legacy BDB wallets deprecated; migration to descriptor wallets required.

---

## Structure

```
src/wallet/
├── bdb.cpp, bdb.h          # BerkeleyDB storage (deprecated)
├── db.cpp, db.h            # Database abstraction layer
├── wallet.cpp, wallet.h    # Core wallet class (CWallet)
├── crypto/                 # Key encryption (crypter.cpp/h)
├── rpc/                    # Wallet RPC implementations
│   ├── wallet.cpp          # Main wallet RPC (migratewallet)
│   └── wallet.h
├── test/                   # Wallet unit tests
└── receive.cpp, coinselection.cpp  # UI/payment handling
```

---

## Where to Look

| Task | File | Notes |
|------|------|-------|
| **Wallet class** | `wallet.h`, `wallet.cpp` | CWallet definition |
| **BDB storage** | `bdb.cpp` | Legacy wallet persistence |
| **SQLite** | `db.cpp` | Modern descriptor wallets |
| **Key encryption** | `crypto/crypter.cpp` | Passphrase handling |
| **Coin selection** | `coinselection.cpp` | UTXO selection algorithm |
| **RPC** | `rpc/wallet.cpp` | migratewallet, listunspent |
| **Wallet loading** | `load.cpp`, `init.cpp` | Startup initialization |

---

## Key Symbols

| Symbol | Type | Role |
|--------|------|------|
| `CWallet` | class | Main wallet interface |
| `CKeyStore` | class | Key storage interface |
| `CCrypter` | class | AES-256 encryption |
| `CoinsSelection` | struct | UTXO selection |
| `COutput` | struct | spendable output |

---

## Conventions

- **Legacy deprecation**: All wallet code must support descriptor migration
- **Encryption**: Uses libsodium crypto_box (X25519-XSalsa20-Poly1305)
- **Database**: Dual backend (BDB deprecated, SQLite preferred)

---

## ⚠️ CMake Migration (29.x → 30.x)

**Bitcoin Core 30.x migrated from Autotools to CMake.**

**When migrating wallet module to CMake**:
- Preserve BDB 6.2 support (REQUIRED for Blackcoin More)
- Never remove USE_BDB conditional compilation
- Preserve static fees (no fee estimation)
- Never enable RBF wallet signaling

**Reference**: `CMake_MIGRATION.md` for complete build system migration details.

---

## Anti-Patterns (THIS MODULE)

- **NEVER**: Create new BDB wallets (deprecated)
- **NEVER**: Use `accounts` API (replaced by labels)
- **NEVER**: Skip passphrase backup warnings
- **NEVER**: Import untrusted metadata/files

---

## Commands

```bash
# Build wallet
make blackmore-wallet

# Wallet RPC
blackmore-cli help wallet        # List wallet commands
blackmore-cli migratewallet      # BDB → descriptor
blackmore-cli listunspent        # List spendable outputs
```

---

*Generated by /init-deep*
