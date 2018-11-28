// Copyright (c) 2012-2016 The Bitcoin Core developers
// Copyright (c) 2016-2019 The MagnaChain Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "address/addrman.h"
#include "test/test_magnachain.h"
#include <string>
#include <boost/test/unit_test.hpp>

#include "coding/hash.h"
#include "net/netbase.h"
#include "misc/random.h"

class CellAddrManTest : public CellAddrMan
{
    uint64_t state;

public:
    CellAddrManTest(bool makeDeterministic = true)
    {
        state = 1;

        if (makeDeterministic) {
            //  Set addrman addr placement to be deterministic.
            MakeDeterministic();
        }
    }

    //! Ensure that bucket placement is always the same for testing purposes.
    void MakeDeterministic()
    {
        nKey.SetNull();
        insecure_rand = FastRandomContext(true);
    }

    int RandomInt(int nMax) override
    {
        state = (CellHashWriter(SER_GETHASH, 0) << state).GetHash().GetCheapHash();
        return (unsigned int)(state % nMax);
    }

    CellAddrInfo* Find(const CellNetAddr& addr, int* pnId = nullptr)
    {
        return CellAddrMan::Find(addr, pnId);
    }

    CellAddrInfo* Create(const CellAddress& addr, const CellNetAddr& addrSource, int* pnId = nullptr)
    {
        return CellAddrMan::Create(addr, addrSource, pnId);
    }

    void Delete(int nId)
    {
        CellAddrMan::Delete(nId);
    }
};

static CellNetAddr ResolveIP(const char* ip)
{
    CellNetAddr addr;
    BOOST_CHECK_MESSAGE(LookupHost(ip, addr, false), strprintf("failed to resolve: %s", ip));
    return addr;
}

static CellNetAddr ResolveIP(std::string ip)
{
    return ResolveIP(ip.c_str());
}

static CellService ResolveService(const char* ip, int port = 0)
{
    CellService serv;
    BOOST_CHECK_MESSAGE(Lookup(ip, serv, port, false), strprintf("failed to resolve: %s:%i", ip, port));
    return serv;
}

static CellService ResolveService(std::string ip, int port = 0)
{
    return ResolveService(ip.c_str(), port);
}

BOOST_FIXTURE_TEST_SUITE(addrman_tests, BasicTestingSetup)

BOOST_AUTO_TEST_CASE(addrman_simple)
{
    CellAddrManTest addrman;

    CellNetAddr source = ResolveIP("252.2.2.2");

    // Test: Does Addrman respond correctly when empty.
    BOOST_CHECK_EQUAL(addrman.size(), 0);
    CellAddrInfo addr_null = addrman.Select();
    BOOST_CHECK_EQUAL(addr_null.ToString(), "[::]:0");

    // Test: Does Addrman::Add work as expected.
    CellService addr1 = ResolveService("250.1.1.1", 8333);
    BOOST_CHECK(addrman.Add(CellAddress(addr1, NODE_NONE), source));
    BOOST_CHECK_EQUAL(addrman.size(), 1);
    CellAddrInfo addr_ret1 = addrman.Select();
    BOOST_CHECK_EQUAL(addr_ret1.ToString(), "250.1.1.1:8333");

    // Test: Does IP address deduplication work correctly.
    //  Expected dup IP should not be added.
    CellService addr1_dup = ResolveService("250.1.1.1", 8333);
    BOOST_CHECK(!addrman.Add(CellAddress(addr1_dup, NODE_NONE), source));
    BOOST_CHECK_EQUAL(addrman.size(), 1);


    // Test: New table has one addr and we add a diff addr we should
    //  have at least one addr.
    // Note that addrman's size cannot be tested reliably after insertion, as
    // hash collisions may occur. But we can always be sure of at least one
    // success.

    CellService addr2 = ResolveService("250.1.1.2", 8333);
    BOOST_CHECK(addrman.Add(CellAddress(addr2, NODE_NONE), source));
    BOOST_CHECK(addrman.size() >= 1);

    // Test: AddrMan::Clear() should empty the new table.
    addrman.Clear();
    BOOST_CHECK_EQUAL(addrman.size(), 0);
    CellAddrInfo addr_null2 = addrman.Select();
    BOOST_CHECK_EQUAL(addr_null2.ToString(), "[::]:0");

    // Test: AddrMan::Add multiple addresses works as expected
    std::vector<CellAddress> vAddr;
    vAddr.push_back(CellAddress(ResolveService("250.1.1.3", 8333), NODE_NONE));
    vAddr.push_back(CellAddress(ResolveService("250.1.1.4", 8333), NODE_NONE));
    BOOST_CHECK(addrman.Add(vAddr, source));
    BOOST_CHECK(addrman.size() >= 1);
}

BOOST_AUTO_TEST_CASE(addrman_ports)
{
    CellAddrManTest addrman;

    CellNetAddr source = ResolveIP("252.2.2.2");

    BOOST_CHECK_EQUAL(addrman.size(), 0);

    // Test 7; Addr with same IP but diff port does not replace existing addr.
    CellService addr1 = ResolveService("250.1.1.1", 8333);
    addrman.Add(CellAddress(addr1, NODE_NONE), source);
    BOOST_CHECK_EQUAL(addrman.size(), 1);

    CellService addr1_port = ResolveService("250.1.1.1", 8334);
    addrman.Add(CellAddress(addr1_port, NODE_NONE), source);
    BOOST_CHECK_EQUAL(addrman.size(), 1);
    CellAddrInfo addr_ret2 = addrman.Select();
    BOOST_CHECK_EQUAL(addr_ret2.ToString(), "250.1.1.1:8333");

    // Test: Add same IP but diff port to tried table, it doesn't get added.
    //  Perhaps this is not ideal behavior but it is the current behavior.
    addrman.Good(CellAddress(addr1_port, NODE_NONE));
    BOOST_CHECK_EQUAL(addrman.size(), 1);
    bool newOnly = true;
    CellAddrInfo addr_ret3 = addrman.Select(newOnly);
    BOOST_CHECK_EQUAL(addr_ret3.ToString(), "250.1.1.1:8333");
}


BOOST_AUTO_TEST_CASE(addrman_select)
{
    CellAddrManTest addrman;

    CellNetAddr source = ResolveIP("252.2.2.2");

    // Test: Select from new with 1 addr in new.
    CellService addr1 = ResolveService("250.1.1.1", 8333);
    addrman.Add(CellAddress(addr1, NODE_NONE), source);
    BOOST_CHECK_EQUAL(addrman.size(), 1);

    bool newOnly = true;
    CellAddrInfo addr_ret1 = addrman.Select(newOnly);
    BOOST_CHECK_EQUAL(addr_ret1.ToString(), "250.1.1.1:8333");

    // Test: move addr to tried, select from new expected nothing returned.
    addrman.Good(CellAddress(addr1, NODE_NONE));
    BOOST_CHECK_EQUAL(addrman.size(), 1);
    CellAddrInfo addr_ret2 = addrman.Select(newOnly);
    BOOST_CHECK_EQUAL(addr_ret2.ToString(), "[::]:0");

    CellAddrInfo addr_ret3 = addrman.Select();
    BOOST_CHECK_EQUAL(addr_ret3.ToString(), "250.1.1.1:8333");

    BOOST_CHECK_EQUAL(addrman.size(), 1);


    // Add three addresses to new table.
    CellService addr2 = ResolveService("250.3.1.1", 8333);
    CellService addr3 = ResolveService("250.3.2.2", 9999);
    CellService addr4 = ResolveService("250.3.3.3", 9999);

    addrman.Add(CellAddress(addr2, NODE_NONE), ResolveService("250.3.1.1", 8333));
    addrman.Add(CellAddress(addr3, NODE_NONE), ResolveService("250.3.1.1", 8333));
    addrman.Add(CellAddress(addr4, NODE_NONE), ResolveService("250.4.1.1", 8333));

    // Add three addresses to tried table.
    CellService addr5 = ResolveService("250.4.4.4", 8333);
    CellService addr6 = ResolveService("250.4.5.5", 7777);
    CellService addr7 = ResolveService("250.4.6.6", 8333);

    addrman.Add(CellAddress(addr5, NODE_NONE), ResolveService("250.3.1.1", 8333));
    addrman.Good(CellAddress(addr5, NODE_NONE));
    addrman.Add(CellAddress(addr6, NODE_NONE), ResolveService("250.3.1.1", 8333));
    addrman.Good(CellAddress(addr6, NODE_NONE));
    addrman.Add(CellAddress(addr7, NODE_NONE), ResolveService("250.1.1.3", 8333));
    addrman.Good(CellAddress(addr7, NODE_NONE));

    // Test: 6 addrs + 1 addr from last test = 7.
    BOOST_CHECK_EQUAL(addrman.size(), 7);

    // Test: Select pulls from new and tried regardless of port number.
    std::set<uint16_t> ports;
    for (int i = 0; i < 20; ++i) {
        ports.insert(addrman.Select().GetPort());
    }
    BOOST_CHECK_EQUAL(ports.size(), 3);
}

BOOST_AUTO_TEST_CASE(addrman_new_collisions)
{
    CellAddrManTest addrman;

    CellNetAddr source = ResolveIP("252.2.2.2");

    BOOST_CHECK_EQUAL(addrman.size(), 0);

    for (unsigned int i = 1; i < 18; i++) {
        CellService addr = ResolveService("250.1.1." + boost::to_string(i));
        addrman.Add(CellAddress(addr, NODE_NONE), source);

        //Test: No collision in new table yet.
        BOOST_CHECK_EQUAL(addrman.size(), i);
    }

    //Test: new table collision!
    CellService addr1 = ResolveService("250.1.1.18");
    addrman.Add(CellAddress(addr1, NODE_NONE), source);
    BOOST_CHECK_EQUAL(addrman.size(), 17);

    CellService addr2 = ResolveService("250.1.1.19");
    addrman.Add(CellAddress(addr2, NODE_NONE), source);
    BOOST_CHECK_EQUAL(addrman.size(), 18);
}

BOOST_AUTO_TEST_CASE(addrman_tried_collisions)
{
    CellAddrManTest addrman;

    CellNetAddr source = ResolveIP("252.2.2.2");

    BOOST_CHECK_EQUAL(addrman.size(), 0);

    for (unsigned int i = 1; i < 80; i++) {
        CellService addr = ResolveService("250.1.1." + boost::to_string(i));
        addrman.Add(CellAddress(addr, NODE_NONE), source);
        addrman.Good(CellAddress(addr, NODE_NONE));

        //Test: No collision in tried table yet.
        BOOST_CHECK_EQUAL(addrman.size(), i);
    }

    //Test: tried table collision!
    CellService addr1 = ResolveService("250.1.1.80");
    addrman.Add(CellAddress(addr1, NODE_NONE), source);
    BOOST_CHECK_EQUAL(addrman.size(), 79);

    CellService addr2 = ResolveService("250.1.1.81");
    addrman.Add(CellAddress(addr2, NODE_NONE), source);
    BOOST_CHECK_EQUAL(addrman.size(), 80);
}

BOOST_AUTO_TEST_CASE(addrman_find)
{
    CellAddrManTest addrman;

    BOOST_CHECK_EQUAL(addrman.size(), 0);

    CellAddress addr1 = CellAddress(ResolveService("250.1.2.1", 8333), NODE_NONE);
    CellAddress addr2 = CellAddress(ResolveService("250.1.2.1", 9999), NODE_NONE);
    CellAddress addr3 = CellAddress(ResolveService("251.255.2.1", 8333), NODE_NONE);

    CellNetAddr source1 = ResolveIP("250.1.2.1");
    CellNetAddr source2 = ResolveIP("250.1.2.2");

    addrman.Add(addr1, source1);
    addrman.Add(addr2, source2);
    addrman.Add(addr3, source1);

    // Test: ensure Find returns an IP matching what we searched on.
    CellAddrInfo* info1 = addrman.Find(addr1);
    BOOST_REQUIRE(info1);
    BOOST_CHECK_EQUAL(info1->ToString(), "250.1.2.1:8333");

    // Test 18; Find does not discriminate by port number.
    CellAddrInfo* info2 = addrman.Find(addr2);
    BOOST_REQUIRE(info2);
    BOOST_CHECK_EQUAL(info2->ToString(), info1->ToString());

    // Test: Find returns another IP matching what we searched on.
    CellAddrInfo* info3 = addrman.Find(addr3);
    BOOST_REQUIRE(info3);
    BOOST_CHECK_EQUAL(info3->ToString(), "251.255.2.1:8333");
}

BOOST_AUTO_TEST_CASE(addrman_create)
{
    CellAddrManTest addrman;

    BOOST_CHECK_EQUAL(addrman.size(), 0);

    CellAddress addr1 = CellAddress(ResolveService("250.1.2.1", 8333), NODE_NONE);
    CellNetAddr source1 = ResolveIP("250.1.2.1");

    int nId;
    CellAddrInfo* pinfo = addrman.Create(addr1, source1, &nId);

    // Test: The result should be the same as the input addr.
    BOOST_CHECK_EQUAL(pinfo->ToString(), "250.1.2.1:8333");

    CellAddrInfo* info2 = addrman.Find(addr1);
    BOOST_CHECK_EQUAL(info2->ToString(), "250.1.2.1:8333");
}


BOOST_AUTO_TEST_CASE(addrman_delete)
{
    CellAddrManTest addrman;

    BOOST_CHECK_EQUAL(addrman.size(), 0);

    CellAddress addr1 = CellAddress(ResolveService("250.1.2.1", 8333), NODE_NONE);
    CellNetAddr source1 = ResolveIP("250.1.2.1");

    int nId;
    addrman.Create(addr1, source1, &nId);

    // Test: Delete should actually delete the addr.
    BOOST_CHECK_EQUAL(addrman.size(), 1);
    addrman.Delete(nId);
    BOOST_CHECK_EQUAL(addrman.size(), 0);
    CellAddrInfo* info2 = addrman.Find(addr1);
    BOOST_CHECK(info2 == nullptr);
}

BOOST_AUTO_TEST_CASE(addrman_getaddr)
{
    CellAddrManTest addrman;

    // Test: Sanity check, GetAddr should never return anything if addrman
    //  is empty.
    BOOST_CHECK_EQUAL(addrman.size(), 0);
    std::vector<CellAddress> vAddr1 = addrman.GetAddr();
    BOOST_CHECK_EQUAL(vAddr1.size(), 0);

    CellAddress addr1 = CellAddress(ResolveService("250.250.2.1", 8333), NODE_NONE);
    addr1.nTime = GetAdjustedTime(); // Set time so isTerrible = false
    CellAddress addr2 = CellAddress(ResolveService("250.251.2.2", 9999), NODE_NONE);
    addr2.nTime = GetAdjustedTime();
    CellAddress addr3 = CellAddress(ResolveService("251.252.2.3", 8333), NODE_NONE);
    addr3.nTime = GetAdjustedTime();
    CellAddress addr4 = CellAddress(ResolveService("252.253.3.4", 8333), NODE_NONE);
    addr4.nTime = GetAdjustedTime();
    CellAddress addr5 = CellAddress(ResolveService("252.254.4.5", 8333), NODE_NONE);
    addr5.nTime = GetAdjustedTime();
    CellNetAddr source1 = ResolveIP("250.1.2.1");
    CellNetAddr source2 = ResolveIP("250.2.3.3");

    // Test: Ensure GetAddr works with new addresses.
    addrman.Add(addr1, source1);
    addrman.Add(addr2, source2);
    addrman.Add(addr3, source1);
    addrman.Add(addr4, source2);
    addrman.Add(addr5, source1);

    // GetAddr returns 23% of addresses, 23% of 5 is 1 rounded down.
    BOOST_CHECK_EQUAL(addrman.GetAddr().size(), 1);

    // Test: Ensure GetAddr works with new and tried addresses.
    addrman.Good(CellAddress(addr1, NODE_NONE));
    addrman.Good(CellAddress(addr2, NODE_NONE));
    BOOST_CHECK_EQUAL(addrman.GetAddr().size(), 1);

    // Test: Ensure GetAddr still returns 23% when addrman has many addrs.
    for (unsigned int i = 1; i < (8 * 256); i++) {
        int octet1 = i % 256;
        int octet2 = i >> 8 % 256;
        std::string strAddr = boost::to_string(octet1) + "." + boost::to_string(octet2) + ".1.23";
        CellAddress addr = CellAddress(ResolveService(strAddr), NODE_NONE);

        // Ensure that for all addrs in addrman, isTerrible == false.
        addr.nTime = GetAdjustedTime();
        addrman.Add(addr, ResolveIP(strAddr));
        if (i % 8 == 0)
            addrman.Good(addr);
    }
    std::vector<CellAddress> vAddr = addrman.GetAddr();

    size_t percent23 = (addrman.size() * 23) / 100;
    BOOST_CHECK_EQUAL(vAddr.size(), percent23);
    BOOST_CHECK_EQUAL(vAddr.size(), 461);
    // (Addrman.size() < number of addresses added) due to address collisions.
    BOOST_CHECK_EQUAL(addrman.size(), 2006);
}


BOOST_AUTO_TEST_CASE(caddrinfo_get_tried_bucket)
{
    CellAddrManTest addrman;

    CellAddress addr1 = CellAddress(ResolveService("250.1.1.1", 8333), NODE_NONE);
    CellAddress addr2 = CellAddress(ResolveService("250.1.1.1", 9999), NODE_NONE);

    CellNetAddr source1 = ResolveIP("250.1.1.1");


    CellAddrInfo info1 = CellAddrInfo(addr1, source1);

    uint256 nKey1 = (uint256)(CellHashWriter(SER_GETHASH, 0) << 1).GetHash();
    uint256 nKey2 = (uint256)(CellHashWriter(SER_GETHASH, 0) << 2).GetHash();


    BOOST_CHECK_EQUAL(info1.GetTriedBucket(nKey1), 40);

    // Test: Make sure key actually randomizes bucket placement. A fail on
    //  this test could be a security issue.
    BOOST_CHECK(info1.GetTriedBucket(nKey1) != info1.GetTriedBucket(nKey2));

    // Test: Two addresses with same IP but different ports can map to
    //  different buckets because they have different keys.
    CellAddrInfo info2 = CellAddrInfo(addr2, source1);

    BOOST_CHECK(info1.GetKey() != info2.GetKey());
    BOOST_CHECK(info1.GetTriedBucket(nKey1) != info2.GetTriedBucket(nKey1));

    std::set<int> buckets;
    for (int i = 0; i < 255; i++) {
        CellAddrInfo infoi = CellAddrInfo(
            CellAddress(ResolveService("250.1.1." + boost::to_string(i)), NODE_NONE),
            ResolveIP("250.1.1." + boost::to_string(i)));
        int bucket = infoi.GetTriedBucket(nKey1);
        buckets.insert(bucket);
    }
    // Test: IP addresses in the same group (\16 prefix for IPv4) should
    //  never get more than 8 buckets
    BOOST_CHECK_EQUAL(buckets.size(), 8);

    buckets.clear();
    for (int j = 0; j < 255; j++) {
        CellAddrInfo infoj = CellAddrInfo(
            CellAddress(ResolveService("250." + boost::to_string(j) + ".1.1"), NODE_NONE),
            ResolveIP("250." + boost::to_string(j) + ".1.1"));
        int bucket = infoj.GetTriedBucket(nKey1);
        buckets.insert(bucket);
    }
    // Test: IP addresses in the different groups should map to more than
    //  8 buckets.
    BOOST_CHECK_EQUAL(buckets.size(), 160);
}

BOOST_AUTO_TEST_CASE(caddrinfo_get_new_bucket)
{
    CellAddrManTest addrman;

    CellAddress addr1 = CellAddress(ResolveService("250.1.2.1", 8333), NODE_NONE);
    CellAddress addr2 = CellAddress(ResolveService("250.1.2.1", 9999), NODE_NONE);

    CellNetAddr source1 = ResolveIP("250.1.2.1");

    CellAddrInfo info1 = CellAddrInfo(addr1, source1);

    uint256 nKey1 = (uint256)(CellHashWriter(SER_GETHASH, 0) << 1).GetHash();
    uint256 nKey2 = (uint256)(CellHashWriter(SER_GETHASH, 0) << 2).GetHash();

    // Test: Make sure the buckets are what we expect
    BOOST_CHECK_EQUAL(info1.GetNewBucket(nKey1), 786);
    BOOST_CHECK_EQUAL(info1.GetNewBucket(nKey1, source1), 786);

    // Test: Make sure key actually randomizes bucket placement. A fail on
    //  this test could be a security issue.
    BOOST_CHECK(info1.GetNewBucket(nKey1) != info1.GetNewBucket(nKey2));

    // Test: Ports should not effect bucket placement in the addr
    CellAddrInfo info2 = CellAddrInfo(addr2, source1);
    BOOST_CHECK(info1.GetKey() != info2.GetKey());
    BOOST_CHECK_EQUAL(info1.GetNewBucket(nKey1), info2.GetNewBucket(nKey1));

    std::set<int> buckets;
    for (int i = 0; i < 255; i++) {
        CellAddrInfo infoi = CellAddrInfo(
            CellAddress(ResolveService("250.1.1." + boost::to_string(i)), NODE_NONE),
            ResolveIP("250.1.1." + boost::to_string(i)));
        int bucket = infoi.GetNewBucket(nKey1);
        buckets.insert(bucket);
    }
    // Test: IP addresses in the same group (\16 prefix for IPv4) should
    //  always map to the same bucket.
    BOOST_CHECK_EQUAL(buckets.size(), 1);

    buckets.clear();
    for (int j = 0; j < 4 * 255; j++) {
        CellAddrInfo infoj = CellAddrInfo(CellAddress(
                                        ResolveService(
                                            boost::to_string(250 + (j / 255)) + "." + boost::to_string(j % 256) + ".1.1"), NODE_NONE),
            ResolveIP("251.4.1.1"));
        int bucket = infoj.GetNewBucket(nKey1);
        buckets.insert(bucket);
    }
    // Test: IP addresses in the same source groups should map to no more
    //  than 64 buckets.
    BOOST_CHECK(buckets.size() <= 64);

    buckets.clear();
    for (int p = 0; p < 255; p++) {
        CellAddrInfo infoj = CellAddrInfo(
            CellAddress(ResolveService("250.1.1.1"), NODE_NONE),
            ResolveIP("250." + boost::to_string(p) + ".1.1"));
        int bucket = infoj.GetNewBucket(nKey1);
        buckets.insert(bucket);
    }
    // Test: IP addresses in the different source groups should map to more
    //  than 64 buckets.
    BOOST_CHECK(buckets.size() > 64);
}
BOOST_AUTO_TEST_SUITE_END()
