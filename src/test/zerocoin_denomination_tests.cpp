// Copyright (c) 2017-2020 The PIVX developers
// Copyright (c) 2021 The DECENOMY Core Developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "amount.h"
#include "chainparams.h"
#include "coincontrol.h"
#include "consensus/zerocoin_verify.h"
#include "denomination_functions.h"
#include "main.h"
#include "txdb.h"
#include "wallet/wallet.h"
#include "wallet/walletdb.h"
#include "test/test_pivx.h"
#include <boost/test/unit_test.hpp>
#include <iostream>


BOOST_FIXTURE_TEST_SUITE(zerocoin_denom_tests, BasicTestingSetup)

//translation from cap quantity to zerocoin denomination
BOOST_AUTO_TEST_CASE(amount_to_denomination_test)
{
    std::cout << "Running amount_to_denomination_test...\n";

    //valid amount (min edge)
    CAmount amount = 1 * COIN;
    BOOST_CHECK_MESSAGE(libzerocoin::AmountToZerocoinDenomination(amount) == libzerocoin::ZQ_ONE, "For COIN denomination should be ZQ_ONE");

    //valid amount (max edge)
    CAmount amount1 = 5000 * COIN;
    BOOST_CHECK_MESSAGE(libzerocoin::AmountToZerocoinDenomination(amount1) == libzerocoin::ZQ_FIVE_THOUSAND, "For 5000*COIN denomination should be ZQ_ONE");

    //invalid amount (too much)
    CAmount amount2 = 7000 * COIN;
    BOOST_CHECK_MESSAGE(libzerocoin::AmountToZerocoinDenomination(amount2) == libzerocoin::ZQ_ERROR, "For 7000*COIN denomination should be Invalid -> ZQ_ERROR");

    //invalid amount (not enough)
    CAmount amount3 = 1;
    BOOST_CHECK_MESSAGE(libzerocoin::AmountToZerocoinDenomination(amount3) == libzerocoin::ZQ_ERROR, "For 1 denomination should be Invalid -> ZQ_ERROR");
}

BOOST_AUTO_TEST_CASE(denomination_to_value_test)
{
    std::cout << "Running ZerocoinDenominationToValue_test...\n";

    int64_t Value = 1 * COIN;
    libzerocoin::CoinDenomination denomination = libzerocoin::ZQ_ONE;
    BOOST_CHECK_MESSAGE(libzerocoin::ZerocoinDenominationToAmount(denomination) == Value, "Wrong Value - should be 1");

    Value = 10 * COIN;
    denomination = libzerocoin::ZQ_TEN;
    BOOST_CHECK_MESSAGE(libzerocoin::ZerocoinDenominationToAmount(denomination) == Value, "Wrong Value - should be 10");

    Value = 50 * COIN;
    denomination = libzerocoin::ZQ_FIFTY;
    BOOST_CHECK_MESSAGE(libzerocoin::ZerocoinDenominationToAmount(denomination) == Value, "Wrong Value - should be 50");

    Value = 500 * COIN;
    denomination = libzerocoin::ZQ_FIVE_HUNDRED;
    BOOST_CHECK_MESSAGE(libzerocoin::ZerocoinDenominationToAmount(denomination) == Value, "Wrong Value - should be 500");

    Value = 100 * COIN;
    denomination = libzerocoin::ZQ_ONE_HUNDRED;
    BOOST_CHECK_MESSAGE(libzerocoin::ZerocoinDenominationToAmount(denomination) == Value, "Wrong Value - should be 100");

    Value = 0 * COIN;
    denomination = libzerocoin::ZQ_ERROR;
    BOOST_CHECK_MESSAGE(libzerocoin::ZerocoinDenominationToAmount(denomination) == Value, "Wrong Value - should be 0");
}

BOOST_AUTO_TEST_CASE(zerocoin_spend_test241)
{
    const int nMaxNumberOfSpends = 4;
    const int DenomAmounts[] = {1, 2, 3, 4, 0, 0, 0, 0};
    CAmount nSelectedValue;
    std::list<CMintMeta> listMints;
    std::map<libzerocoin::CoinDenomination, CAmount> mapDenom;

    int j = 0;
    CAmount nTotalAmount = 0;
    int CoinsHeld = 0;

    // Create a set of Minted coins that fits profile given by DenomAmounts
    // Also setup Map array corresponding to DenomAmount which is the current set of coins available

    for (const auto& denom : libzerocoin::zerocoinDenomList) {
        for (int i = 0; i < DenomAmounts[j]; i++) {
            CAmount currentAmount = libzerocoin::ZerocoinDenominationToAmount(denom);
            nTotalAmount += currentAmount;
            CBigNum value;
            CBigNum rand;
            CBigNum serial = CBigNum::randKBitBignum(256);
            bool isUsed = false;
            CMintMeta meta;
            meta.denom = denom;
            meta.hashPubcoin = GetPubCoinHash(value);
            meta.hashSerial = GetSerialHash(serial);
            meta.isUsed = isUsed;
            meta.nVersion = 1;
            listMints.push_back(meta);
        }
        mapDenom.insert(std::pair<libzerocoin::CoinDenomination, CAmount>(denom, DenomAmounts[j]));
        j++;
    }
    CoinsHeld = nTotalAmount / COIN;
    std::cout << "Curremt Amount held = " << CoinsHeld << ": ";

    // Show what we have
    j = 0;
    for (const auto& denom : libzerocoin::zerocoinDenomList)
        std::cout << DenomAmounts[j++] << "*" << libzerocoin::ZerocoinDenominationToAmount(denom) / COIN << " + ";
    std::cout << "\n";

    // For DenomAmounts[] = {1,2,3,4,0,0,0,0}; we can spend up to 200 without requiring more than 4 Spends
    // Amounts above this can not be met
    CAmount MaxLimit = 200;
    CAmount OneCoinAmount = libzerocoin::ZerocoinDenominationToAmount(libzerocoin::ZQ_ONE);
    CAmount nValueTarget = OneCoinAmount;
    int nCoinsReturned;
    int nNeededSpends = 0;  // Number of spends which would be needed if selection failed

    bool fDebug = 0;

    // Go through all possible spend between 1 and 241 and see if it's possible or not
    for (int i = 0; i < CoinsHeld; i++) {
        std::vector<CMintMeta> vSpends = SelectMintsFromList(nValueTarget, nSelectedValue,
                                                                 nMaxNumberOfSpends,
                                                                 nCoinsReturned,
                                                                 listMints,
                                                                 mapDenom,
                                                                 nNeededSpends);

        if (fDebug) {
            if (vSpends.size() > 0) {
                std::cout << "SUCCESS : Coins = " << nValueTarget / COIN << " # spends used = " << vSpends.size()
                << " # of coins returned = " << nCoinsReturned
                          << " Spend Amount = " << nSelectedValue / COIN << " Held = " << CoinsHeld << "\n";
            } else {
                std::cout << "FAILED : Coins = " << nValueTarget / COIN << " Held = " << CoinsHeld << "\n";
            }
        }

        if (i < MaxLimit) {
            BOOST_CHECK_MESSAGE(vSpends.size() < 5, "Too many spends");
            BOOST_CHECK_MESSAGE(vSpends.size() > 0, "No spends");
        } else {
            bool spends_not_ok = ((vSpends.size() >= 4) || (vSpends.size() == 0));
            BOOST_CHECK_MESSAGE(spends_not_ok, "Expected to fail but didn't");
        }
        nValueTarget += OneCoinAmount;
    }
    //std::cout << "241 Test done!\n";
}
BOOST_AUTO_TEST_CASE(zerocoin_spend_test115)
{
    const int nMaxNumberOfSpends = 4;
    const int DenomAmounts[] = {0, 1, 1, 2, 0, 0, 0, 0};
    CAmount nSelectedValue;
    std::list<CMintMeta> listMints;
    std::map<libzerocoin::CoinDenomination, CAmount> mapDenom;

    int j = 0;
    CAmount nTotalAmount = 0;
    int CoinsHeld = 0;

    // Create a set of Minted coins that fits profile given by DenomAmounts
    // Also setup Map array corresponding to DenomAmount which is the current set of coins available
    for (const auto& denom : libzerocoin::zerocoinDenomList) {
        for (int i = 0; i < DenomAmounts[j]; i++) {
            CAmount currentAmount = libzerocoin::ZerocoinDenominationToAmount(denom);
            nTotalAmount += currentAmount;
            CBigNum value;
            CBigNum rand;
            CBigNum serial = CBigNum::randKBitBignum(256);
            bool isUsed = false;
            CMintMeta meta;
            meta.denom = denom;
            meta.hashPubcoin = GetPubCoinHash(value);
            meta.hashSerial = GetSerialHash(serial);
            meta.isUsed = isUsed;
            meta.nVersion = 1;
            listMints.push_back(meta);
        }
        mapDenom.insert(std::pair<libzerocoin::CoinDenomination, CAmount>(denom, DenomAmounts[j]));
        j++;
    }
    CoinsHeld = nTotalAmount / COIN;
    std::cout << "Curremt Amount held = " << CoinsHeld << ": ";

    // Show what we have
    j = 0;
    for (const auto& denom : libzerocoin::zerocoinDenomList)
        std::cout << DenomAmounts[j++] << "*" << libzerocoin::ZerocoinDenominationToAmount(denom) / COIN << " + ";
    std::cout << "\n";

    CAmount OneCoinAmount = libzerocoin::ZerocoinDenominationToAmount(libzerocoin::ZQ_ONE);
    CAmount nValueTarget = OneCoinAmount;

    //bool fDebug = 0;
    int nCoinsReturned;
    int nNeededSpends = 0;  // Number of spends which would be needed if selection failed

    std::vector<CMintMeta> vSpends = SelectMintsFromList(nValueTarget, nSelectedValue,
                                                             nMaxNumberOfSpends,
                                                             nCoinsReturned,
                                                             listMints,
                                                             mapDenom,
                                                             nNeededSpends);

//    if (fDebug) {
//        if (vSpends.size() > 0) {
//            std::cout << "SUCCESS : Coins = " << nValueTarget / COIN << " # spends used = " << vSpends.size()
//            << " # of coins returned = " << nCoinsReturned
//                      << " Spend Amount = " << nSelectedValue / COIN << " Held = " << CoinsHeld << "\n";
//        } else {
//            std::cout << "FAILED : Coins = " << nValueTarget / COIN << " Held = " << CoinsHeld << "\n";
//        }
//    }

    BOOST_CHECK_MESSAGE(vSpends.size() < 5, "Too many spends");
    BOOST_CHECK_MESSAGE(vSpends.size() > 0, "No spends");
    nValueTarget += OneCoinAmount;
}
BOOST_AUTO_TEST_CASE(zerocoin_spend_test_from_245)
{
    const int nMaxNumberOfSpends = 5;
    // For 36:
    // const int nSpendValue = 36;
    // Here have a 50 so use for 36 since can't meet exact amount
    //    const int DenomAmounts[] = {0,1,4,1,0,0,0,0};
    // Here have 45 so use 4*10 for 36 since can't meet exact amount
    //    const int DenomAmounts[] = {0, 1, 4, 0, 0, 0, 0, 0};
    // For 51
    //const int nSpendValue = 51;

    // CoinsHeld = 245
    const int DenomAmounts[] = {0, 1, 4, 2, 1, 0, 0, 0};
    // We can spend up to this amount for above set for less 6 spends
    // Otherwise, 6 spends are required
    const int nMaxSpendAmount = 220;
    CAmount nSelectedValue;
    std::list<CMintMeta> listMints;
    std::map<libzerocoin::CoinDenomination, CAmount> mapOfDenomsHeld;

    int j = 0;
    CAmount nTotalAmount = 0;
    int CoinsHeld = 0;

    // Create a set of Minted coins that fits profile given by DenomAmounts
    // Also setup Map array corresponding to DenomAmount which is the current set of coins available
    for (const auto& denom : libzerocoin::zerocoinDenomList) {
        for (int i = 0; i < DenomAmounts[j]; i++) {
            CAmount currentAmount = libzerocoin::ZerocoinDenominationToAmount(denom);
            nTotalAmount += currentAmount;
            CBigNum value;
            CBigNum rand;
            CBigNum serial = CBigNum::randKBitBignum(256);
            bool isUsed = false;
            CMintMeta meta;
            meta.denom = denom;
            meta.hashPubcoin = GetPubCoinHash(value);
            meta.hashSerial = GetSerialHash(serial);
            meta.isUsed = isUsed;
            meta.nVersion = 1;
            listMints.push_back(meta);
        }
        mapOfDenomsHeld.insert(std::pair<libzerocoin::CoinDenomination, CAmount>(denom, DenomAmounts[j]));
        j++;
    }
    CoinsHeld = nTotalAmount / COIN;
    std::cout << "Curremt Amount held = " << CoinsHeld << ": ";

    // Show what we have
    j = 0;
    for (const auto& denom : libzerocoin::zerocoinDenomList)
        std::cout << DenomAmounts[j++] << "*" << libzerocoin::ZerocoinDenominationToAmount(denom) / COIN << " + ";
    std::cout << "\n";

    CAmount OneCoinAmount = libzerocoin::ZerocoinDenominationToAmount(libzerocoin::ZQ_ONE);
    CAmount nValueTarget = OneCoinAmount;

  //  bool fDebug = 0;
    int nCoinsReturned;
    int nNeededSpends = 0;  // Number of spends which would be needed if selection failed

    // Go through all possible spend between 1 and 241 and see if it's possible or not
    for (int i = 0; i < CoinsHeld; i++) {
        std::vector<CMintMeta> vSpends = SelectMintsFromList(nValueTarget, nSelectedValue,
                                                                 nMaxNumberOfSpends,
                                                                 nCoinsReturned,
                                                                 listMints,
                                                                 mapOfDenomsHeld,
                                                                 nNeededSpends);

//        if (fDebug) {
//            if (vSpends.size() > 0) {
//                std::cout << "SUCCESS : Coins = " << nValueTarget / COIN << " # spends = " << vSpends.size()
//                          << " # coins returned = " << nCoinsReturned
//                          << " Amount = " << nSelectedValue / COIN << " Held = " << CoinsHeld << " ";
//            } else {
//                std::cout << "UNABLE TO SPEND : Coins = " << nValueTarget / COIN << " Held = " << CoinsHeld << "\n";
//            }
//        }

        bool spends_not_ok = ((vSpends.size() > nMaxNumberOfSpends) || (vSpends.size() == 0));
        if (i < nMaxSpendAmount) BOOST_CHECK_MESSAGE(!spends_not_ok, "Too many spends");
        else BOOST_CHECK_MESSAGE(spends_not_ok, "Expected to fail but didn't");

        std::vector<CMintMeta> vSpendsAlt = SelectMintsFromList(nValueTarget, nSelectedValue,
                                                                    nMaxNumberOfSpends,
                                                                    nCoinsReturned,
                                                                    listMints,
                                                                    mapOfDenomsHeld,
                                                                    nNeededSpends);


//        if (fDebug) {
//            if (vSpendsAlt.size() > 0) {
//                std::cout << "# spends = " << vSpendsAlt.size()
//                          << " # coins returned = " << nCoinsReturned
//                          << " Amount = " << nSelectedValue / COIN << "\n";
//            } else {
//                std::cout << "UNABLE TO SPEND : Coins = " << nValueTarget / COIN << " Held = " << CoinsHeld << "\n";
//            }
//        }

        spends_not_ok = ((vSpendsAlt.size() > nMaxNumberOfSpends) || (vSpendsAlt.size() == 0));
        if (i < nMaxSpendAmount) BOOST_CHECK_MESSAGE(!spends_not_ok, "Too many spends");
        else BOOST_CHECK_MESSAGE(spends_not_ok, "Expected to fail but didn't");

        nValueTarget += OneCoinAmount;
    }
}


BOOST_AUTO_TEST_CASE(zerocoin_spend_test_from_145)
{
    const int nMaxNumberOfSpends = 5;
    // CoinsHeld = 145
    const int DenomAmounts[] = {0, 1, 4, 2, 0, 0, 0, 0};
    CAmount nSelectedValue;
    std::list<CMintMeta> listMints;
    std::map<libzerocoin::CoinDenomination, CAmount> mapOfDenomsHeld;

    int j = 0;
    CAmount nTotalAmount = 0;
    int CoinsHeld = 0;

    // Create a set of Minted coins that fits profile given by DenomAmounts
    // Also setup Map array corresponding to DenomAmount which is the current set of coins available
    for (const auto& denom : libzerocoin::zerocoinDenomList) {
        for (int i = 0; i < DenomAmounts[j]; i++) {
            CAmount currentAmount = libzerocoin::ZerocoinDenominationToAmount(denom);
            nTotalAmount += currentAmount;
            CBigNum value;
            CBigNum rand;
            CBigNum serial = CBigNum::randKBitBignum(256);
            bool isUsed = false;
            CMintMeta meta;
            meta.denom = denom;
            meta.hashPubcoin = GetPubCoinHash(value);
            meta.hashSerial = GetSerialHash(serial);
            meta.isUsed = isUsed;
            meta.nVersion = 1;
            listMints.push_back(meta);
        }
        mapOfDenomsHeld.insert(std::pair<libzerocoin::CoinDenomination, CAmount>(denom, DenomAmounts[j]));
        j++;
    }
    CoinsHeld = nTotalAmount / COIN;
    std::cout << "Curremt Amount held = " << CoinsHeld << ": ";
    // We can spend up to this amount for above set for less 6 spends
    // Otherwise, 6 spends are required
    const int nMaxSpendAmount = 130;

    // Show what we have
    j = 0;
    for (const auto& denom : libzerocoin::zerocoinDenomList)
        std::cout << DenomAmounts[j++] << "*" << libzerocoin::ZerocoinDenominationToAmount(denom) / COIN << " + ";
    std::cout << "\n";

    CAmount OneCoinAmount = libzerocoin::ZerocoinDenominationToAmount(libzerocoin::ZQ_ONE);
    CAmount nValueTarget = OneCoinAmount;

    //bool fDebug = 0;
    int nCoinsReturned;
    int nNeededSpends = 0;  // Number of spends which would be needed if selection failed

    // Go through all possible spend between 1 and 241 and see if it's possible or not
    for (int i = 0; i < CoinsHeld; i++) {
        std::vector<CMintMeta> vSpends = SelectMintsFromList(nValueTarget, nSelectedValue,
                                                                 nMaxNumberOfSpends,
                                                                 nCoinsReturned,
                                                                 listMints,
                                                                 mapOfDenomsHeld,
                                                                 nNeededSpends);

//        if (fDebug) {
//            if (vSpends.size() > 0) {
//                std::cout << "SUCCESS : Coins = " << nValueTarget / COIN << " # spends = " << vSpends.size()
//                          << " # coins returned = " << nCoinsReturned
//                          << " Amount = " << nSelectedValue / COIN << " Held = " << CoinsHeld << " ";
//            } else {
//                std::cout << "UNABLE TO SPEND : Coins = " << nValueTarget / COIN << " Held = " << CoinsHeld << "\n";
//            }
//        }

        bool spends_not_ok = ((vSpends.size() > nMaxNumberOfSpends) || (vSpends.size() == 0));
        if (i < nMaxSpendAmount) BOOST_CHECK_MESSAGE(!spends_not_ok, "Too many spends");
        else BOOST_CHECK_MESSAGE(spends_not_ok, "Expected to fail but didn't");

        std::vector<CMintMeta> vSpendsAlt = SelectMintsFromList(nValueTarget, nSelectedValue,
                                                                    nMaxNumberOfSpends,
                                                                    nCoinsReturned,
                                                                    listMints,
                                                                    mapOfDenomsHeld,
                                                                    nNeededSpends);


//        if (fDebug) {
//            if (vSpendsAlt.size() > 0) {
//                std::cout << "# spends = " << vSpendsAlt.size()
//                          << " # coins returned = " << nCoinsReturned
//                          << " Amount = " << nSelectedValue / COIN << "\n";
//            } else {
//                std::cout << "UNABLE TO SPEND : Coins = " << nValueTarget / COIN << " Held = " << CoinsHeld << "\n";
//            }
//        }

        spends_not_ok = ((vSpendsAlt.size() > nMaxNumberOfSpends) || (vSpendsAlt.size() == 0));
        if (i < nMaxSpendAmount) BOOST_CHECK_MESSAGE(!spends_not_ok, "Too many spends");
        else BOOST_CHECK_MESSAGE(spends_not_ok, "Expected to fail but didn't");


        nValueTarget += OneCoinAmount;
    }
}


BOOST_AUTO_TEST_CASE(zerocoin_spend_test99)
{
    const int nMaxNumberOfSpends = 4;
    const int DenomAmounts[] = {0, 1, 4, 2, 1, 0, 0, 0};
    CAmount nSelectedValue;
    std::list<CMintMeta> listMints;
    std::map<libzerocoin::CoinDenomination, CAmount> mapOfDenomsHeld;

    int j = 0;
    CAmount nTotalAmount = 0;
    int CoinsHeld = 0;

    // Create a set of Minted coins that fits profile given by DenomAmounts
    // Also setup Map array corresponding to DenomAmount which is the current set of coins available
    for (const auto& denom : libzerocoin::zerocoinDenomList) {
        for (int i = 0; i < DenomAmounts[j]; i++) {
            CAmount currentAmount = libzerocoin::ZerocoinDenominationToAmount(denom);
            nTotalAmount += currentAmount;
            CBigNum value;
            CBigNum rand;
            CBigNum serial = CBigNum::randKBitBignum(256);
            bool isUsed = false;
            CMintMeta meta;
            meta.denom = denom;
            meta.hashPubcoin = GetPubCoinHash(value);
            meta.hashSerial = GetSerialHash(serial);
            meta.isUsed = isUsed;
            meta.nVersion = 1;
            listMints.push_back(meta);
        }
        mapOfDenomsHeld.insert(std::pair<libzerocoin::CoinDenomination, CAmount>(denom, DenomAmounts[j]));
        j++;
    }
    CoinsHeld = nTotalAmount / COIN;
    std::cout << "Curremt Amount held = " << CoinsHeld << ": ";

    // Show what we have
    j = 0;
    for (const auto& denom : libzerocoin::zerocoinDenomList)
        std::cout << DenomAmounts[j++] << "*" << libzerocoin::ZerocoinDenominationToAmount(denom) / COIN << " + ";
    std::cout << "\n";

    CAmount OneCoinAmount = libzerocoin::ZerocoinDenominationToAmount(libzerocoin::ZQ_ONE);
    CAmount nValueTarget = 99 * OneCoinAmount;

//    bool fDebug = 0;
    int nCoinsReturned;
    int nNeededSpends = 0;  // Number of spends which would be needed if selection failed

    std::vector<CMintMeta> vSpends = SelectMintsFromList(nValueTarget, nSelectedValue,
                                                             nMaxNumberOfSpends,
                                                             nCoinsReturned,
                                                             listMints,
                                                             mapOfDenomsHeld,
                                                             nNeededSpends);

//    if (fDebug) {
//        if (vSpends.size() > 0) {
//            std::cout << "SUCCESS : Coins = " << nValueTarget / COIN << " # spends used = " << vSpends.size()
//            << " # of coins returned = " << nCoinsReturned
//                      << " Spend Amount = " << nSelectedValue / COIN << " Held = " << CoinsHeld << "\n";
//        } else {
//            std::cout << "FAILED : Coins = " << nValueTarget / COIN << " Held = " << CoinsHeld << "\n";
//        }
//    }

    BOOST_CHECK_MESSAGE(vSpends.size() < 5, "Too many spends");
    BOOST_CHECK_MESSAGE(vSpends.size() > 0, "No spends");
    nValueTarget += OneCoinAmount;
}

BOOST_AUTO_TEST_SUITE_END()
