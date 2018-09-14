// This source file is part of the polarphp.org open source project
//
// Copyright (c) 2017 - 2018 polarphp software foundation
// Copyright (c) 2017 - 2018 zzu_softboy <zzu_softboy@163.com>
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See http://polarphp.org/LICENSE.txt for license information
// See http://polarphp.org/CONTRIBUTORS.txt for the list of polarphp project authors
//
// Created by polarboy on 2018/09/05.

#include "Discovery.h"
#include "LitConfig.h"
#include "Test.h"
#include <filesystem>

namespace fs = std::filesystem;

namespace polar {
namespace lit {

using StringMap = std::map<std::string, std::string>;

std::optional<std::string> choose_config_file_from_dir(const std::string &dir,
                                                       const std::list<std::string> &configNames)
{
   for (const std::string &name : configNames) {
      fs::path p(dir);
      p /= name;
      if (fs::exists(p)) {
         return p;
      }
   }
   return std::nullopt;
}

std::optional<std::string> dir_contains_test_suite(const std::string &path,
                                                   const LitConfig &config)
{
   std::optional<std::string> cfgPath = choose_config_file_from_dir(path, config.getSiteConfigNames());
   if (!cfgPath.has_value()) {
      cfgPath = choose_config_file_from_dir(path, config.getConfigNames());
   }
   return cfgPath;
}

namespace {

TestSuitSearchResult search_testsuit(const std::string &path,
                                     const LitConfig &config,
                                     std::map<std::string, TestSuitSearchResult> &cache);

TestSuitSearchResult do_search_testsuit(const std::string &path,
                                        const LitConfig &config,
                                        std::map<std::string, TestSuitSearchResult> &cache)
{
   // Check for a site config or a lit config.
   std::optional<std::string> cfgPathOpt = dir_contains_test_suite(path, config);
   // If we didn't find a config file, keep looking.
   std::string base;
   if (!cfgPathOpt.has_value()) {
      fs::path fsPath(path);
      std::string parent = fsPath.parent_path();
      base = fsPath.filename();
      if (parent == path) {
         return TestSuitSearchResult{std::nullopt, std::list<std::string>{}};
      }
      TestSuitSearchResult temp = search_testsuit(parent, config, cache);
      std::get<1>(temp).push_back(base);
      return temp;
   }
   // This is a private builtin parameter which can be used to perform
   // translation of configuration paths.  Specifically, this parameter
   // can be set to a dictionary that the discovery process will consult
   // when it finds a configuration it is about to load.  If the given
   // path is in the map, the value of that key is a path to the
   // configuration to load instead.
   std::any configMapAny = config.getParams().at("config_map");
   std::string cfgPath;
   if (configMapAny.has_value()) {
      cfgPath = fs::canonical(cfgPathOpt.value());
      StringMap &configMap = std::any_cast<StringMap&>(configMapAny);
      if (configMap.find(cfgPath) != configMap.end()) {
         cfgPath = configMap.at(cfgPath);
      }
   }
   // We found a test suite, create a new config for it and load it.
   if (config.isDebug()) {
      config.note(format_string("loading suite config %s", cfgPath.c_str()), __FILE__, __LINE__);
   }
   TestingConfig testingCfg = TestingConfig::fromDefaults(config);
   testingCfg.loadFromPath(cfgPath, config);
   std::string sourceRoot;
   std::string execRoot;
   if (testingCfg.getTestSourceRoot().has_value()) {
      sourceRoot = testingCfg.getTestSourceRoot().value();
   } else {
      sourceRoot = path;
   }
   if (testingCfg.getTestExecRoot().has_value()) {
      execRoot = testingCfg.getTestExecRoot().value();
   } else {
      execRoot = path;
   }
   return TestSuitSearchResult{TestSuite(testingCfg.getName(), sourceRoot, execRoot, testingCfg), std::list<std::string>{}};
}

TestSuitSearchResult search_testsuit(const std::string &path,
                                     const LitConfig &config,
                                     std::map<std::string, TestSuitSearchResult> &cache)
{
   // Check for an already instantiated test suite.
   std::string realPath = fs::canonical(path);
   if (cache.find(realPath) == cache.end()) {
      cache[realPath] = do_search_testsuit(path, config, cache);
   }
   return cache.at(realPath);
}

} // anonymous namespace

TestSuitSearchResult get_test_suite(std::string item, const LitConfig &config,
                                    std::map<std::string, TestSuitSearchResult> &cache)
{
   // Canonicalize the path.
   item = fs::canonical(fs::current_path() / item);
   // Skip files and virtual components.
   std::list<std::string> components;
   fs::path currentDir(item);
   while (!fs::is_directory(currentDir)) {
      std::string parent = currentDir.parent_path();
      std::string base = currentDir.filename();
      if (parent == currentDir.string()) {
         return TestSuitSearchResult{std::nullopt, std::list<std::string>{}};
      }
      components.push_back(base);
      currentDir = parent;
   }
   components.reverse();
   TestSuitSearchResult temp = search_testsuit(currentDir.string(), config, cache);
   for (const std::string &item : components) {
      std::get<1>(temp).push_back(item);
   }
   return temp;
}

namespace {

TestingConfig search_local_config(const TestSuite &testSuit, const LitConfig &litConfig,
                                  const std::list<std::string> &pathInSuite)
{
   TestingConfig parent;
   if (pathInSuite.empty()) {
      parent = testSuit.getConfig();
   } else {
      std::list<std::string> paths = pathInSuite;
      paths.pop_back();
      parent = search_local_config(testSuit, litConfig, paths);
   }
   std::string sourcePath = testSuit.getSourcePath(pathInSuite);
   std::optional<std::string> cfgPath = choose_config_file_from_dir(sourcePath, litConfig.getLocalConfigNames());
   // If not, just reuse the parent config.
   if (!cfgPath.has_value()){
      return parent;
   }
   // Otherwise, copy the current config and load the local configuration
   // file into it.
   TestingConfig config = parent;
   if (litConfig.isDebug()) {
       litConfig.note(format_string("loading local config %s", cfgPath.value().c_str()));
   }
   config.loadFromPath(cfgPath.value(), litConfig);
   return config;
}

} // anonymous namespacs

} // lit
} // polar
