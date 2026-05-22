#include "app/Application.h"

int main(int argc, char* argv[])
{
    Application app;
    if (argc > 1)
        app.OpenFile(argv[1]);
    return app.Run();
}
