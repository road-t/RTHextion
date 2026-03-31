#include "editorsession.h"
#include "hexdocument.h"

EditorSession::EditorSession()
{
}

EditorSession::~EditorSession()
{
    delete document;
    // editor is owned by QTabWidget parent — Qt handles deletion
}
