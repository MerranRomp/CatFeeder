#include "Fakes.h"

#include <iostream>

int main() {
    simulation::MockAccess access;
    simulation::MockDispenser dispenser;
    simulation::MemoryHistory history;
    catfeeder::Controller controller(simulation::mockConfig(), access, dispenser, history, 0);

    controller.update(0, simulation::mockInputs(0));
    std::cout << "Mock identification alone: " << dispenser.requests << " dispenses\n";
    std::cout << "Explicit access request accepted: "
              << controller.requestAccess(10, simulation::mockInputs(10)) << '\n';
    access.reported = catfeeder::OperationStatus::Complete;
    controller.update(20, simulation::mockInputs(20));
    std::cout << "Explicit dispensing request accepted: "
              << controller.requestDispense(30, simulation::mockInputs(30)) << '\n';
    dispenser.reported = catfeeder::OperationStatus::Complete;
    controller.update(40, simulation::mockInputs(40));
    std::cout << "Immediate repeat accepted: "
              << controller.requestDispense(50, simulation::mockInputs(50)) << '\n';
    std::cout << "Simulation only: no physical devices were used.\n";
}
