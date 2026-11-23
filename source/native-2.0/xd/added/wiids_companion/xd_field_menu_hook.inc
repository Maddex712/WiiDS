// Native ROM-version/unlock predicate, shared by field quick-open admission.
extern "C" int WiidsXD_FieldPdaAllowed() { return _menuFieldCheckPDA()!=0; }
