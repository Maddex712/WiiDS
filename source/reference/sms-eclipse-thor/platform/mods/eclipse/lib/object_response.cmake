# Keep custom partial links below cmd.exe's command-length limit. Generator
# expressions resolve the object libraries after their output paths are known.
# A comma in a path given as text (a folder such as "Recomps, Decomp and other
# ports") would end JOIN's list there, so it is spelled $<COMMA>.
function(sms_object_response output filename)
  string(REPLACE "," "$<COMMA>" items "${ARGN}")
  file(GENERATE OUTPUT "${filename}"
    CONTENT "\"$<JOIN:${items},\"\n\">\"\n")
  set(${output} "@${filename}" PARENT_SCOPE)
endfunction()
