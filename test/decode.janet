#!/usr/bin/env janet

(import yyjson)

(def testdata ["canada.json"
               "citm_catalog.json"
               "twitter.json"])

(each name testdata (let [input (slurp (string "./test/" name))]
                      (assert (table? (yyjson/decode input)))))
