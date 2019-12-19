#include <fstream>
#include <iostream>
#include <sstream>
#include <string>


std::string stringify( std::string const & line ) {

    bool inconstant=false;

    std::stringstream s;
    for (int i=0; i<(int)line.size(); ++i) {

        // escape double quotes
        if (line[i]=='"') {
            s << '\\' ;
            inconstant = inconstant ? false : true;
        }

        if (line[i]=='\\' && line[i+1]=='\0') {
            s << "\"";
            return s.str();
        }

        // escape backslash
        if (inconstant && line[i]=='\\')
           s << '\\' ;

        s << line[i];
    }

    s << "\\n\"";

    return s.str();
}

int main(int argc, char **argv) {

    if (argc != 3) {
        std::cerr << "Usage: stringify input-file output-file" << std::endl;
        return 1;
    }

    std::ifstream input;
    input.open(argv[1]);
    if (! input.is_open()) {
        std::cerr << "Can not read from: " << argv[1] << std::endl;
        return 1;
    }

    std::ofstream output;
    output.open(argv[2]);
    if (! output.is_open()) {
        std::cerr << "Can not write to: " << argv[2] << std::endl;
        return 1;
    }

    std::string line;

    while (! input.eof()) {
        std::getline(input, line);
        output << "\"" << stringify(line) << std::endl;
    }

    return 0;
}
