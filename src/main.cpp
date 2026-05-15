#include <clocale>
#include <iostream>
#include <string>

#include "http_server.h"

int main() {
    std::setlocale(LC_ALL, "ru_RU.UTF-8");

    const std::string host = "127.0.0.1";
    const int port = 8080;
    const std::string output_dir = "output/latest";

    std::cout << "Timetable Solver API\n";
    return timetable::RunApiServer(host, port, output_dir);
}
