/* Copyright 2019 The Spin-Scenario Authors. All Rights Reserved.
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at
    http://www.apache.org/licenses/LICENSE-2.0
Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#include <ssl.h>
#include <linenoise.h>
#ifdef WIN32
#include <Windows.h>
#endif

const char *examples[] = {
    "load(\'\')", "lua", "quit", "power", NULL
};

void completionHook(char const *prefix, linenoiseCompletions *lc) {
  for (size_t i = 0; examples[i] != NULL; ++i) {
    if (strncmp(prefix, examples[i], strlen(prefix)) == 0) {
      linenoiseAddCompletion(lc, examples[i]);
    }
  }
}
int main(int argc, char *argv[]) {
  char s[256];
#ifdef WIN32
  GetCurrentDirectoryA(256, s);
#else
  s = get_current_dir_name();
#endif
  set_ssl_usr_dir(s);

  int flag = 0;
  sol::state lua;;
  lua.open_libraries();
  ssl::bindings(lua);
  load_yacas();
  yacas_global_vars();
  load_expmv_theta();
  ssl_version_output();
  init_global_lua(lua);

  linenoiseInstallWindowChangeHandler();
  while (argc > 1) {
    argc--;
    argv++;
    if (!strcmp(*argv, "--keycodes")) {
      linenoisePrintKeyCodes();
      exit(0);
    }
  }

  const char *file = "history";
  linenoiseHistoryLoad(file);
  linenoiseSetCompletionCallback(completionHook);
  printf("\n");
  char const *prompt = "\x1b[1;32mspin-scenario\x1b[0m> ";
  boost::cmatch what;
  boost::regex reg_load("load\\s*\\(\\s*'(.*?)'\\s*\\)");
  label:
  try {
    while (1) {
      char *result = linenoise(prompt);

      if (result == NULL) {
        break;
      } else if (!strncmp(result, "/history", 8)) {
        /* Display the current history. */
        for (int index = 0;; ++index) {
          char *hist = linenoiseHistoryLine(index);
          if (hist == NULL) break;
          printf("%4d: %s\n", index, hist);
          free(hist);
        }
      }
      if (*result == '\0') {
        free(result);
        break;
      }
      linenoiseHistoryAdd(result);
      linenoiseHistorySave(file);

      string line = string(result);
      string s = boost::to_lower_copy(line);
      if (s == "q" || s == "quit" || s == "exit")
        break;
      // in case of running lua code from file.
      if (boost::regex_search(line.c_str(), what, reg_load)) {
        lua.script("script_transform ('" + string(what[1]) + "')"); // transform the script file into 'temp.lua'.
        lua.script_file("temp.lua");
        lua.script("os.remove('temp.lua')");
      } else
        lua.script(line);

      free(result);
    }
  } catch (const std::exception &e) {
    string s = boost::str(boost::format("%s\n") % string(e.what()));
    ssl_color_text("err", s);
    flag = std::system("pause");
    goto label;
  }
  linenoiseHistoryFree();
  return flag;
}
