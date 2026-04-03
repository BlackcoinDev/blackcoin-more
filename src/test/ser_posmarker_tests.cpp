// Copyright (c) 2012-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <streams.h>
#include <primitives/block.h>
#include <chain.h>
#include <kernel/chain.h>
#include <kernel/cs_main.h>
#include <test/util/setup_common.h>

#include <boost/test/unit_test.hpp>

#include <vector>
#include <cstdint>

BOOST_FIXTURE_TEST_SUITE(ser_posmarker_tests, BasicTestingSetup)

// Test 1: DataStream basic serialization/deserialization
BOOST_AUTO_TEST_CASE(datastream_basic)
{
    // Test basic DataStream operations with SetType
    DataStream ss{};
    ss.SetType(SER_NETWORK);

    // Serialize some basic types
    uint32_t nVersion = 4;
    uint256 hashPrev = uint256::ZERO;
    uint32_t nTime = 1234567890;
    uint32_t nBits = 0x1d00ffff;
    uint32_t nNonce = 12345;

    ss << nVersion << hashPrev << nTime << nBits << nNonce;

    // Verify we can deserialize
    DataStream ss2{ss};

    uint32_t nVersion2;
    uint256 hashPrev2;
    uint32_t nTime2;
    uint32_t nBits2;
    uint32_t nNonce2;

    ss2 >> nVersion2 >> hashPrev2 >> nTime2 >> nBits2 >> nNonce2;

    BOOST_CHECK_EQUAL(nVersion, nVersion2);
    BOOST_CHECK(hashPrev == hashPrev2);
    BOOST_CHECK_EQUAL(nTime, nTime2);
    BOOST_CHECK_EQUAL(nBits, nBits2);
    BOOST_CHECK_EQUAL(nNonce, nNonce2);
}

// Test 2: SER_POSMARKER flag causes nFlags to be included in CBlockHeader serialization
BOOST_AUTO_TEST_CASE(ser_posmarker_includes_nflags)
{
    // Create a CBlockHeader with nFlags set
    CBlockHeader header;
    header.nVersion = 4;
    header.hashPrevBlock = uint256::ZERO;
    header.hashMerkleRoot = uint256::ZERO;
    header.nTime = 1234567890;
    header.nBits = 0x1d00ffff;
    header.nNonce = 12345;
    header.nFlags = 0x04; // BLOCK_PROOF_OF_STAKE

    // Serialize WITH SER_POSMARKER flag
    DataStream ssPosMarker{};
    ssPosMarker.SetType(SER_NETWORK | SER_POSMARKER);
    ssPosMarker << header;

    // Serialize WITHOUT SER_POSMARKER flag
    DataStream ssNoPosMarker{};
    ssNoPosMarker.SetType(SER_NETWORK);
    ssNoPosMarker << header;

    // With SER_POSMARKER, the stream should be larger (nFlags is included)
    // Without SER_POSMARKER, nFlags is NOT serialized
    BOOST_CHECK_MESSAGE(
        ssPosMarker.size() > ssNoPosMarker.size(),
        "SER_POSMARKER should cause nFlags to be serialized, increasing stream size"
    );

    // Deserialize with SER_POSMARKER and verify nFlags is preserved
    // NOTE: DataStream copy constructor does NOT propagate nType,
    // so we must explicitly SetType on the deserialization stream.
    DataStream ssVerify{ssPosMarker};
    ssVerify.SetType(SER_NETWORK | SER_POSMARKER);
    CBlockHeader headerVerify;
    ssVerify >> headerVerify;

    BOOST_CHECK_EQUAL(header.nFlags, headerVerify.nFlags);
    BOOST_CHECK_EQUAL(header.nVersion, headerVerify.nVersion);
    BOOST_CHECK_EQUAL(header.nNonce, headerVerify.nNonce);
}

// Test 3: Without SER_POSMARKER, nFlags is NOT serialized
BOOST_AUTO_TEST_CASE(ser_no_posmarker_excludes_nflags)
{
    // Create a CBlockHeader with nFlags set
    CBlockHeader header;
    header.nVersion = 4;
    header.hashPrevBlock = uint256::ZERO;
    header.hashMerkleRoot = uint256::ZERO;
    header.nTime = 1234567890;
    header.nBits = 0x1d00ffff;
    header.nNonce = 12345;
    header.nFlags = 0x04; // BLOCK_PROOF_OF_STAKE

    // Serialize WITHOUT SER_POSMARKER
    DataStream ss{};
    ss.SetType(SER_NETWORK);
    ss << header;

    // Deserialize and check that nFlags is NOT the same as original
    // (it should be default/zero since it wasn't serialized)
    // NOTE: DataStream copy constructor does NOT propagate nType,
    // but that's fine here — we intentionally want nType=0 (no SER_POSMARKER)
    // to prove nFlags is excluded from deserialization.
    DataStream ss2{ss};
    ss2.SetType(SER_NETWORK); // Explicitly: no SER_POSMARKER
    CBlockHeader header2;
    ss2 >> header2;

    // nFlags should be 0 (default) since SER_POSMARKER was not set
    BOOST_CHECK_EQUAL(header2.nFlags, 0U);
    BOOST_CHECK_EQUAL(header2.nVersion, header.nVersion);
    BOOST_CHECK_EQUAL(header2.nNonce, header.nNonce);
}

// Test 4: Block signature (vchBlockSig) round-trip with SER_POSMARKER
BOOST_AUTO_TEST_CASE(ser_posmarker_vchblocksig_roundtrip)
{
    // Create a CBlock with signature
    CBlock block;
    block.nVersion = 4;
    block.hashPrevBlock = uint256::ZERO;
    block.hashMerkleRoot = uint256::ZERO;
    block.nTime = 1234567890;
    block.nBits = 0x1d00ffff;
    block.nNonce = 12345;
    block.nFlags = 0x04; // BLOCK_PROOF_OF_STAKE

    // Add a signature (this would normally be set by staking)
    std::vector<unsigned char> vchBlockSig = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'};
    block.vchBlockSig = vchBlockSig;

    // Serialize with SER_POSMARKER
    // NOTE: CBlock contains vtx (vector of CTransaction), and CTransaction::Serialize
    // requires a ParamsStream with GetParams(). Use TX_WITH_WITNESS() wrapper.
    DataStream ss{};
    ss.SetType(SER_NETWORK | SER_POSMARKER);
    ss << TX_WITH_WITNESS(block);

    // Deserialize and verify
    // NOTE: Must SetType on deserialization stream — copy constructor doesn't propagate nType
    DataStream ss2{ss};
    ss2.SetType(SER_NETWORK | SER_POSMARKER);
    CBlock block2;
    ss2 >> TX_WITH_WITNESS(block2);

    BOOST_CHECK_EQUAL(block.nFlags, block2.nFlags);
    BOOST_CHECK_EQUAL(block.vchBlockSig.size(), block2.vchBlockSig.size());
    BOOST_CHECK(block.vchBlockSig == block2.vchBlockSig);
    BOOST_CHECK_EQUAL(block.nVersion, block2.nVersion);
    BOOST_CHECK_EQUAL(block.nNonce, block2.nNonce);
}

// Test 5: nStakeModifier persistence round-trip (CDiskBlockIndex serialization)
// Note: CBlockIndex does NOT have SERIALIZE_METHODS — only CDiskBlockIndex does.
BOOST_AUTO_TEST_CASE(ser_posmarker_stakemodifier_roundtrip)
{
    // CDiskBlockIndex is the on-disk serialization format that includes
    // nFlags and nStakeModifier unconditionally (not gated by SER_POSMARKER).
    CDiskBlockIndex diskIndex;
    diskIndex.nVersion = 4;
    diskIndex.hashPrev = uint256::ZERO;
    diskIndex.hashMerkleRoot = uint256::ZERO;
    diskIndex.nTime = 1234567890;
    diskIndex.nBits = 0x1d00ffff;
    diskIndex.nNonce = 12345;
    diskIndex.nHashBlock = uint256::ZERO;
    diskIndex.nFlags = 0x04; // BLOCK_PROOF_OF_STAKE
    diskIndex.nStakeModifier = uint256::ONE; // Non-zero modifier
    {
        LOCK(::cs_main);
        diskIndex.nStatus = 0; // No BLOCK_HAVE_DATA/UNDO — simplest serialization path
    }

    // Serialize (CDiskBlockIndex always writes nFlags + nStakeModifier)
    DataStream ss{};
    ss << diskIndex;

    // Deserialize and verify PoS fields are preserved
    DataStream ss2{ss};
    CDiskBlockIndex diskIndex2;
    ss2 >> diskIndex2;

    BOOST_CHECK(diskIndex.nStakeModifier == diskIndex2.nStakeModifier);
    BOOST_CHECK_EQUAL(diskIndex.nFlags, diskIndex2.nFlags);
    BOOST_CHECK_EQUAL(diskIndex.nVersion, diskIndex2.nVersion);
    BOOST_CHECK_EQUAL(diskIndex.nNonce, diskIndex2.nNonce);
}

BOOST_AUTO_TEST_SUITE_END()
