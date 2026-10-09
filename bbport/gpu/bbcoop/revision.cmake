# bbport co-op: writes OUT (bbcoop_revision.h) with the source tree's git revision, rewritten only
# when it changed. Run at every build: the log names the build ("Run: Revision <hash>").
execute_process(COMMAND git rev-parse HEAD WORKING_DIRECTORY "${SOURCE}"
                OUTPUT_VARIABLE revision OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if (NOT revision)
    # A jj workspace of its own has no .git: its working copy's parent, as git's HEAD is in the
    # colocated one.
    execute_process(COMMAND jj log --ignore-working-copy --no-graph --limit 1 -r @- -T commit_id
                    WORKING_DIRECTORY "${SOURCE}" OUTPUT_VARIABLE revision
                    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
endif()
if (NOT revision MATCHES "^[0-9a-f]+$")
    set(revision "0000000")
endif()
set(content "#pragma once\n#define BB_SCM_REV \"${revision}\"\n")
if (EXISTS "${OUT}")
    file(READ "${OUT}" previous)
endif()
if (NOT previous STREQUAL content)
    file(WRITE "${OUT}" "${content}")
endif()
