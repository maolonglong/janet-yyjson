(declare-project
  :name "yyjson"
  :description "Janet bindings for yyjson."
  :author "Shaolong Chen"
  :license "MIT"
  :url "https://github.com/maolonglong/janet-yyjson"
  :repo "git+https://github.com/maolonglong/janet-yyjson")

(declare-native
  :name "yyjson"
  :source @["yyjson.c" "bindings.c"])
