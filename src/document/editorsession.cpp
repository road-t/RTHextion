#include "editorsession.h"
#include "hexdocument.h"


EditorSession::~EditorSession()
{
    delete document;
    qDeleteAll(liveTableState.wrappers);
}
