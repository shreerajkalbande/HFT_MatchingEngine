#pragma once

#include <cstdint>

struct BBO
{
    uint32_t best_bid_price = 0;
    uint64_t best_bid_qty = 0;
    uint32_t best_ask_price = 0;
    uint64_t best_ask_qty = 0;
};
