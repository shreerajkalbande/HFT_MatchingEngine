#include <iostream>
#include <string>

#include "MatchingEngine.h"
#include "OrderArena.h"

namespace
{
void printExecution(const Execution& execution, void*)
{
    std::cout << "    trade " << execution.quantity << " @ " << execution.price
              << " (maker #" << execution.maker_order_id
              << ", taker #" << execution.taker_order_id << ")\n";
}

template <typename Engine>
void runDemo(const std::string& backend_name)
{
    OrderArena arena(8);
    Engine engine(8, printExecution);

    std::cout << "\n  " << backend_name << "\n";

    engine.processOrder(arena.allocateOrder(1, 10'100, 40, Side::ASK, 1));
    engine.processOrder(arena.allocateOrder(2, 10'100, 30, Side::ASK, 2));
    engine.processOrder(arena.allocateOrder(3, 10'101, 50, Side::ASK, 3));

    BBO before = engine.getBBO();
    std::cout << "    before: " << before.best_ask_qty
              << " @ " << before.best_ask_price << "\n";

    engine.cancelOrder(2);
    engine.processOrder(arena.allocateOrder(4, 10'101, 60, Side::BID, 4));

    BBO after = engine.getBBO();
    std::cout << "    after:  ";
    if (after.best_ask_price == 0)
        std::cout << "ask book empty\n";
    else
        std::cout << after.best_ask_qty << " @ " << after.best_ask_price << "\n";
}
} // namespace

int main()
{
    std::cout << "\nPrice-Time Priority Matching Engine\n"
              << "Same matching logic, interchangeable price indexes\n";

    runDemo<MapMatchingEngine>("std::map price index");
    runDemo<BitmapMatchingEngine>("hierarchical bitmap price index");

    std::cout << "\nRun `make bench` for the controlled backend comparison.\n\n";
    return 0;
}
