alias t := test

test:
  jpm test

clean:
  rm -rf .jpm_tree build
