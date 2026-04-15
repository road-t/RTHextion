#include "editorsession.h"
#include "hexdocument.h"


EditorSession::EditorSession() = default;

EditorSession::~EditorSession()
{
    delete document;
    qDeleteAll(liveTableState.wrappers);
}
