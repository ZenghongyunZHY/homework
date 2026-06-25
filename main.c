#include "controller/DataController.h"
#include "service/DataService.h"

int main(int argc, char** argv) {
    if (argc >= 2) {
        return data_service_run_cli(argc, argv);
    }
    return controller_run_interactive();
}
