# The screens' words in four languages (Yay 0.1.19 开发文档 4.7): the table packages/strings generates into generated/
# (pnpm strings:build; committed, not edited here) and the one language every screen goes by (yk_words.h). Its own
# component, needing nothing, so that Yay, the recovery and every extension (through yaykit_ui and yaykit_app) link the
# same table; it lives here, not in packages/, because the developer kit carries yaykit/firmware only.
idf_component_register(SRCS "generated/yk_strings.c" "yk_words.c"
    INCLUDE_DIRS "include" "generated")
