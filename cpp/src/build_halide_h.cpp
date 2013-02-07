#include <stdio.h>
#include <string.h>
#include <set>
#include <string>
#include <assert.h>

using namespace std;

set<string> done;

void dump_header(string header) {
    if (done.find(header) != done.end()) return;
    done.insert(header);

    FILE *f = fopen(header.c_str(), "r");

    assert(f);

    char line[1024];

    while (fgets(line, 1024, f)) {
        if (strncmp(line, "#include \"", 10) == 0) {
            char *sub_header = line + 10;
            for (int i = 0; i < 1014; i++) {
                if (sub_header[i] == '"') sub_header[i] = 0;
            }
            dump_header(sub_header);
        } else {
            fputs(line, stdout);
        }
    }

    fclose(f);
}

int main(int argc, char **headers) {
    for (int i = 1; i < argc; i++) {
        dump_header(headers[i]);
    }

    return 0;
}