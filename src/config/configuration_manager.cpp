// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0

#include <regex>

#include <cryptopp/dll.h>
#include <boost/algorithm/string.hpp>
#include <nlohmann/json.hpp>

#include "configuration_manager.hpp"

using std::cerr;
using std::endl;
using std::invalid_argument;
using std::ostream;
using std::string;
using std::to_string;
using std::unordered_set;
using std::vector;

using boost::program_options::command_line_parser;
using boost::program_options::options_description;
using boost::program_options::variables_map;

using log4cplus::Logger;

using nlohmann::json;

using concord::config::ConcordConfiguration;
using concord::config::detectLocalNode;

bool initialize_config(int argc, char** argv, ConcordConfiguration& config_out,
                       variables_map& opts_out) {
  // Holds the file name of path of the configuration file for Concordthis is
  // NOT same as logger configuration file. Logger configuration file can be
  // specified as a property in configuration file.
  string configFile;

  // Program options which are generic for most of the programs:
  // These are not available via configuration files
  // only allowed to be passed on command line
  options_description generic{"Generic Options"};

  // clang-format off
  generic.add_options()
      ("help,h", "Print this help message")
      ("config,c",
       boost::program_options::value<string>(&configFile)->required(),
       "Path for configuration file")
      ("debug", "Sleep for 20 seconds to attach debug");
  // clang-format on

  // First we parse command line options and see if --help
  // options was provided. In this case we don't need to
  // go for parsing config file. Otherwise call notify
  // for command line options and move to parsing config file.
  store(command_line_parser(argc, argv).options(generic).run(), opts_out);

  // If cmdline options specified --help then we don't want
  // to do further processing for command line or
  // config file options
  if (opts_out.count("help")) {
    std::cout << "VMware Project Concord" << std::endl;
    std::cout << generic << std::endl;
    return true;
  }

  // call notify after checking "help", so that required
  // parameters are not required to get help (this call throws an
  // exception to exit the program if any parameters are invalid)
  notify(opts_out);

  // Verify configuration file exists.
  std::ifstream fileInput(configFile);
  if (!fileInput.is_open()) {
    cerr << "Concord could not open configuration file: " << configFile << endl;
    return false;
  }
  if (fileInput.peek() == EOF) {
    cerr << "Concord configuration file " << configFile
         << " appears to be an empty file." << endl;
    return false;
  }

  // Parse configuration file.
  concord::config::specifyConfiguration(config_out);
  config_out.setConfigurationStateLabel("concord_node");
  concord::config::YAMLConfigurationInput input(fileInput);

  try {
    input.parseInput();
  } catch (std::exception& e) {
    std::cerr
        << "An exception occurred while trying to read the configuration file "
        << configFile << ": exception message: " << e.what() << std::endl;
  }

  concord::config::loadNodeConfiguration(config_out, input);
  concord::config::loadSBFTCryptosystems(config_out);

  return true;
}

namespace concord {
namespace config {

// Implementations of member functions for the core configuration library
// classes declared in concord/src/configuration_manager.hpp.

ConfigurationPath::ConfigurationPath()
    : isScope(false), useInstance(false), name(), index(0), subpath() {}

ConfigurationPath::ConfigurationPath(const string& name, bool isScope)
    : isScope(isScope), useInstance(false), name(name), index(0), subpath() {}

ConfigurationPath::ConfigurationPath(const string& name, size_t index)
    : isScope(true), useInstance(true), name(name), index(index), subpath() {}

ConfigurationPath::ConfigurationPath(const ConfigurationPath& other)
    : isScope(other.isScope),
      useInstance(other.useInstance),
      name(other.name),
      index(other.index),
      subpath() {
  if (other.subpath) {
    subpath.reset(new ConfigurationPath(*(other.subpath)));
  }
}

ConfigurationPath::~ConfigurationPath() {}

ConfigurationPath& ConfigurationPath::operator=(
    const ConfigurationPath& other) {
  isScope = other.isScope;
  useInstance = other.useInstance;
  name = other.name;
  index = other.index;
  if (other.subpath) {
    subpath.reset(new ConfigurationPath(*(other.subpath)));
  } else {
    subpath.reset();
  }
  return *this;
}

bool ConfigurationPath::operator==(const ConfigurationPath& other) const {
  bool equal = (isScope == other.isScope) && (name == other.name);
  if (equal && isScope) {
    equal = (useInstance == other.useInstance) &&
            (((bool)subpath) == ((bool)(other.subpath)));
    if (equal && useInstance) {
      equal = (index == other.index);
    }
    if (equal && subpath) {
      equal = (*subpath == *(other.subpath));
    }
  }
  return equal;
}

bool ConfigurationPath::operator!=(const ConfigurationPath& other) const {
  return !(*this == other);
}

string ConfigurationPath::toString() const {
  string str = name;
  if (isScope && useInstance) {
    str += "[" + to_string(index) + "]";
  }
  if (isScope && subpath) {
    str += "/" + subpath->toString();
  }
  return str;
}

bool ConfigurationPath::contains(const ConfigurationPath& other) const {
  if (!(this->isScope)) {
    return false;
  }
  if (!(other.isScope) || (name != other.name) ||
      (useInstance != other.useInstance) ||
      (useInstance && (index != other.index))) {
    return false;
  }
  if (this->subpath) {
    return (other.subpath) && (this->subpath->contains(*(other.subpath)));
  } else {
    return true;
  }
}

ConfigurationPath ConfigurationPath::concatenate(
    const ConfigurationPath& other) const {
  if (!isScope) {
    throw invalid_argument(
        "Attempting to concatenate a configuration path (" + other.toString() +
        ") to a non-scope ConfigurationPath (" + toString() + ").");
  } else {
    ConfigurationPath ret(name, isScope);
    if (isScope && useInstance) {
      ret.useInstance = true;
      ret.index = index;
    }
    if (subpath) {
      ret.subpath = std::unique_ptr<ConfigurationPath>(
          new ConfigurationPath(subpath->concatenate(other)));
    } else {
      ret.subpath =
          std::unique_ptr<ConfigurationPath>(new ConfigurationPath(other));
    }
    return ret;
  }
}

ConfigurationPath ConfigurationPath::getLeaf() const {
  if (!isScope || !subpath) {
    return ConfigurationPath(*this);
  } else {
    return subpath->getLeaf();
  }
}

ConfigurationPath ConfigurationPath::trimLeaf() const {
  ConfigurationPath ret(*this);
  ConfigurationPath* stemEnd = &ret;
  while ((stemEnd->isScope) && (stemEnd->subpath) &&
         (stemEnd->subpath->isScope) && (stemEnd->subpath->subpath)) {
    stemEnd = stemEnd->subpath.get();
  }
  stemEnd->subpath.reset();
  return ret;
}

ConcordConfiguration::ConfigurationScope::ConfigurationScope(
    const ConcordConfiguration::ConfigurationScope& original)
    : instantiated(original.instantiated),
      instances(original.instances),
      description(original.description),
      size(original.size),
      sizerState(original.sizerState) {
  if (original.instanceTemplate) {
    instanceTemplate.reset(
        new ConcordConfiguration(*(original.instanceTemplate)));
  }
}

ConcordConfiguration::ConfigurationScope&
ConcordConfiguration::ConfigurationScope::operator=(
    const ConcordConfiguration::ConfigurationScope& original) {
  instantiated = original.instantiated;
  instances = original.instances;
  description = original.description;
  size = original.size;
  sizerState = original.sizerState;
  if (original.instanceTemplate) {
    instanceTemplate.reset(
        new ConcordConfiguration(*(original.instanceTemplate)));
  }
  return *this;
}

ConcordConfiguration::ConfigurationParameter::ConfigurationParameter(
    const ConcordConfiguration::ConfigurationParameter& other)
    : description(other.description),
      hasDefaultValue(other.hasDefaultValue),
      defaultValue(other.defaultValue),
      initialized(other.initialized),
      value(other.value),
      tags(other.tags),
      validator(other.validator),
      validatorState(other.validatorState),
      generator(other.generator),
      generatorState(other.generatorState) {}

ConcordConfiguration::ConfigurationParameter&
ConcordConfiguration::ConfigurationParameter::operator=(
    const ConcordConfiguration::ConfigurationParameter& other) {
  description = other.description, hasDefaultValue = other.hasDefaultValue;
  defaultValue = other.defaultValue;
  initialized = other.initialized;
  value = other.value;
  tags = other.tags;
  validator = other.validator;
  validatorState = other.validatorState;
  generator = other.generator;
  generatorState = other.generatorState;
  return *this;
}

void ConcordConfiguration::invalidateIterators() {
  for (ConcordConfiguration::Iterator* iterator : iterators) {
    iterator->invalidate();
  }
  iterators.clear();
  if (parentScope) {
    parentScope->invalidateIterators();
  }
}

ConfigurationPath* ConcordConfiguration::getCompletePath(
    const ConfigurationPath& localPath) const {
  if (scopePath) {
    return new ConfigurationPath(scopePath->concatenate(localPath));
  } else {
    return new ConfigurationPath(localPath);
  }
}

string ConcordConfiguration::printCompletePath(
    const ConfigurationPath& localPath) const {
  std::unique_ptr<ConfigurationPath> completePath(getCompletePath(localPath));
  return completePath->toString();
}

string ConcordConfiguration::printCompletePath(
    const string& localParameter) const {
  std::unique_ptr<ConfigurationPath> completePath(
      getCompletePath(ConfigurationPath(localParameter, false)));
  return completePath->toString();
}

void ConcordConfiguration::updateSubscopePaths() {
  for (auto& scope : scopes) {
    ConfigurationPath templatePath(scope.first, true);
    scopes[scope.first].instanceTemplate->scopePath.reset(
        getCompletePath(templatePath));
    scopes[scope.first].instanceTemplate->updateSubscopePaths();
    vector<ConcordConfiguration>& instances = scopes[scope.first].instances;
    for (size_t i = 0; i < instances.size(); ++i) {
      ConfigurationPath instancePath(scope.first, i);
      instances[i].scopePath.reset(getCompletePath(instancePath));
      instances[i].updateSubscopePaths();
    }
  }
}

ConcordConfiguration::ConfigurationParameter&
ConcordConfiguration::getParameter(const string& parameter,
                                   const string& failureMessage) {
  return const_cast<ConfigurationParameter&>(
      (const_cast<const ConcordConfiguration*>(this))
          ->getParameter(parameter, failureMessage));
}

const ConcordConfiguration::ConfigurationParameter&
ConcordConfiguration::getParameter(const string& parameter,
                                   const string& failureMessage) const {
  if (!contains(parameter)) {
    ConfigurationPath path(parameter, false);
    throw ConfigurationResourceNotFoundException(
        failureMessage + printCompletePath(path) + ": parameter not found.");
  }
  return parameters.at(parameter);
}

const ConcordConfiguration* ConcordConfiguration::getRootConfig() const {
  const ConcordConfiguration* rootConfig = this;
  while (rootConfig->parentScope) {
    rootConfig = rootConfig->parentScope;
  }
  return rootConfig;
}

template <>
bool ConcordConfiguration::interpretAs<short>(string value,
                                              short& output) const {
  int intVal;
  try {
    intVal = std::stoi(value);
  } catch (invalid_argument& e) {
    return false;
  } catch (std::out_of_range& e) {
    return false;
  }
  if ((intVal < SHRT_MIN) || (intVal > SHRT_MAX)) {
    return false;
  }
  output = static_cast<short>(intVal);
  return true;
}

template <>
string ConcordConfiguration::getTypeName<short>() const {
  return "short";
}

template <>
bool ConcordConfiguration::interpretAs<string>(string value,
                                               string& output) const {
  output = value;
  return true;
}

template <>
string ConcordConfiguration::getTypeName<string>() const {
  return "string";
}

template <>
bool ConcordConfiguration::interpretAs<uint16_t>(string value,
                                                 uint16_t& output) const {
  // This check is necessary because stoul/stoull actually have semantics for if
  // their input is preceded with a '-' other than throwing an exception.
  if ((value.length() > 0) && value[0] == '-') {
    return false;
  }

  unsigned long long intVal;
  try {
    intVal = std::stoull(value);
  } catch (invalid_argument& e) {
    return false;
  } catch (std::out_of_range& e) {
    return false;
  }
  if (intVal > UINT16_MAX) {
    return false;
  }
  output = static_cast<uint16_t>(intVal);
  return true;
}

template <>
string ConcordConfiguration::getTypeName<uint16_t>() const {
  return "uint16_t";
}

template <>
bool ConcordConfiguration::interpretAs<uint32_t>(string value,
                                                 uint32_t& output) const {
  // This check is necessary because stoul/stoull actually have semantics for if
  // their input is preceded with a '-' other than throwing an exception.
  if ((value.length() > 0) && value[0] == '-') {
    return false;
  }

  unsigned long long intVal;
  try {
    intVal = std::stoull(value);
  } catch (invalid_argument& e) {
    return false;
  } catch (std::out_of_range& e) {
    return false;
  }
  if (intVal > UINT32_MAX) {
    return false;
  }
  output = static_cast<uint32_t>(intVal);
  return true;
}

template <>
string ConcordConfiguration::getTypeName<uint32_t>() const {
  return "uint32_t";
}

template <>
bool ConcordConfiguration::interpretAs<uint64_t>(string value,
                                                 uint64_t& output) const {
  // This check is necessary because stoul/stoull actually have semantics for if
  // their input is preceded with a '-' other than throwing an exception.
  if ((value.length() > 0) && value[0] == '-') {
    return false;
  }

  unsigned long long intVal;
  try {
    intVal = std::stoull(value);
  } catch (invalid_argument& e) {
    return false;
  } catch (std::out_of_range& e) {
    return false;
  }
  if (intVal > UINT64_MAX) {
    return false;
  }
  output = static_cast<uint64_t>(intVal);
  return true;
}

template <>
string ConcordConfiguration::getTypeName<uint64_t>() const {
  return "uint64_t";
}

template <>
bool ConcordConfiguration::interpretAs<int32_t>(string value,
                                                int32_t& output) const {
  long long intVal;
  try {
    intVal = std::stoll(value);
  } catch (invalid_argument& e) {
    return false;
  } catch (std::out_of_range& e) {
    return false;
  }
  if ((intVal > INT32_MAX) || (intVal < INT32_MIN)) {
    return false;
  }
  output = static_cast<int32_t>(intVal);
  return true;
}

template <>
string ConcordConfiguration::getTypeName<int32_t>() const {
  return "int32_t";
}

static const vector<string> kValidBooleansTrue({"t", "T", "true", "True",
                                                "TRUE"});
static const vector<string> kValidBooleansFalse({"f", "F", "false", "False",
                                                 "FALSE"});

template <>
bool ConcordConfiguration::interpretAs<bool>(string value, bool& output) const {
  if (std::find(kValidBooleansTrue.begin(), kValidBooleansTrue.end(), value) !=
      kValidBooleansTrue.end()) {
    output = true;
    return true;
  } else if (std::find(kValidBooleansFalse.begin(), kValidBooleansFalse.end(),
                       value) != kValidBooleansFalse.end()) {
    output = false;
    return true;
  }

  return false;
}

template <>
string ConcordConfiguration::getTypeName<bool>() const {
  return "bool";
}

void ConcordConfiguration::ConfigurationIterator::updateRetVal() {
  if (currentParam != endParams) {
    retVal.name = (*currentParam).first;
    retVal.isScope = false;
    retVal.subpath.reset();
  } else if (currentScope != endScopes) {
    retVal.name = (*currentScope).first;
    retVal.isScope = true;
    if (usingInstance) {
      retVal.useInstance = true;
      retVal.index = instance;
    } else {
      retVal.useInstance = false;
    }
    if (currentScopeContents && endCurrentScope &&
        (*currentScopeContents != *endCurrentScope)) {
      retVal.subpath.reset(new ConfigurationPath(**currentScopeContents));
    } else {
      retVal.subpath.reset();
    }
  }
}

ConcordConfiguration::ConfigurationIterator::ConfigurationIterator()
    : recursive(false),
      scopes(false),
      parameters(false),
      instances(false),
      templates(false),
      config(nullptr),
      retVal(),
      currentScope(),
      endScopes(),
      usingInstance(false),
      instance(0),
      currentScopeContents(),
      endCurrentScope(),
      currentParam(),
      endParams(),
      invalid(false) {}

ConcordConfiguration::ConfigurationIterator::ConfigurationIterator(
    ConcordConfiguration& configuration, bool recursive, bool scopes,
    bool parameters, bool instances, bool templates, bool end)
    : recursive(recursive),
      scopes(scopes),
      parameters(parameters),
      instances(instances),
      templates(templates),
      config(&configuration),
      retVal(),
      endScopes(configuration.scopes.end()),
      usingInstance(false),
      instance(0),
      currentScopeContents(),
      endCurrentScope(),
      endParams(configuration.parameters.end()),
      invalid(false) {
  if (!end && parameters) {
    currentParam = configuration.parameters.begin();
  } else {
    currentParam = configuration.parameters.end();
  }
  if (!end && (recursive || scopes)) {
    currentScope = configuration.scopes.begin();

    // If the above initializations leave this iterator in a state where it is
    // pointing to a path that it cannot return, then we call ++ to advance it
    // to the first point where it can actually return something (if any).
    if ((currentParam == endParams) && (currentScope != endScopes) &&
        (!scopes || !templates)) {
      ++(*this);
    }
  } else {
    currentScope = configuration.scopes.end();
  }

  updateRetVal();
  configuration.registerIterator(this);
}

ConcordConfiguration::ConfigurationIterator::ConfigurationIterator(
    const ConcordConfiguration::ConfigurationIterator& original)
    : recursive(original.recursive),
      scopes(original.scopes),
      parameters(original.parameters),
      instances(original.instances),
      templates(original.templates),
      config(original.config),
      retVal(original.retVal),
      currentScope(original.currentScope),
      endScopes(original.endScopes),
      usingInstance(original.usingInstance),
      instance(original.instance),
      currentScopeContents(),
      endCurrentScope(),
      currentParam(original.currentParam),
      endParams(original.endParams),
      invalid(original.invalid) {
  if (original.currentScopeContents) {
    currentScopeContents.reset(new ConcordConfiguration::ConfigurationIterator(
        *(original.currentScopeContents)));
  }
  if (original.endCurrentScope) {
    endCurrentScope.reset(new ConcordConfiguration::ConfigurationIterator(
        *(original.endCurrentScope)));
  }

  if (config && !invalid) {
    config->registerIterator(this);
  }
}

ConcordConfiguration::ConfigurationIterator::~ConfigurationIterator() {
  if (config && !invalid) {
    config->deregisterIterator(this);
  }
  currentScopeContents.reset();
  endCurrentScope.reset();
}

ConcordConfiguration::ConfigurationIterator&
ConcordConfiguration::ConfigurationIterator::operator=(
    const ConcordConfiguration::ConfigurationIterator& original) {
  if (config && !invalid) {
    config->deregisterIterator(this);
  }

  recursive = original.recursive;
  scopes = original.scopes;
  parameters = original.parameters;
  instances = original.instances;
  templates = original.templates;
  config = original.config;
  retVal = original.retVal;
  currentScope = original.currentScope;
  endScopes = original.endScopes;
  usingInstance = original.usingInstance;
  instance = original.instance;
  if (original.currentScopeContents) {
    currentScopeContents.reset(new ConcordConfiguration::ConfigurationIterator(
        *(original.currentScopeContents)));
  } else {
    currentScopeContents.reset();
  }
  if (original.endCurrentScope) {
    endCurrentScope.reset(new ConcordConfiguration::ConfigurationIterator(
        *(original.endCurrentScope)));
  } else {
    endCurrentScope.reset();
  }
  currentParam = original.currentParam;
  endParams = original.endParams;
  invalid = original.invalid;

  if (config && !invalid) {
    config->registerIterator(this);
  }

  return *this;
}

bool ConcordConfiguration::ConfigurationIterator::operator==(
    const ConcordConfiguration::ConfigurationIterator& other) const {
  bool ret = (recursive == other.recursive) && (scopes == other.scopes) &&
             (parameters == other.parameters) &&
             (instances == other.instances) && (templates == other.templates) &&
             (config == other.config) && (currentScope == other.currentScope) &&
             (endScopes == other.endScopes) &&
             (currentParam == other.currentParam) &&
             (endParams == other.endParams) && (invalid == other.invalid);

  // Note we ignore scope-specific state if we are done with all the scopes and
  // we ignore instance-specific state if we are not currently using an
  // instance.
  if (ret && (currentScope != endScopes)) {
    ret = (usingInstance == other.usingInstance);
    if (ret && usingInstance) {
      ret = instance == other.instance;
    }
    if (ret) {
      if (!(currentScopeContents && other.currentScopeContents)) {
        ret = currentScopeContents == other.currentScopeContents;
      } else {
        ret = *currentScopeContents == *(other.currentScopeContents);
      }
    }
    if (ret) {
      if (!(endCurrentScope && other.endCurrentScope)) {
        ret = endCurrentScope == other.endCurrentScope;
      } else {
        ret = *endCurrentScope == *(other.endCurrentScope);
      }
    }
  }
  return ret;
}

bool ConcordConfiguration::ConfigurationIterator::operator!=(
    const ConcordConfiguration::ConfigurationIterator& other) const {
  return (!(*this == other));
}

const ConfigurationPath& ConcordConfiguration::ConfigurationIterator::
operator*() const {
  if (invalid) {
    throw InvalidIteratorException(
        "Attempting to use an iterator over a ConcordConfiguration that has "
        "been modified since the iterator's creation.");
  }
  if ((currentScope == endScopes) && (currentParam == endParams)) {
    // This iterator is either empty or pointing to the end of the configuration
    // if this case is reached.
    throw std::out_of_range(
        "Attempting to access value at iterator already at the end of a "
        "ConcordConfiguration.");
  }
  return retVal;
}

ConcordConfiguration::ConfigurationIterator&
ConcordConfiguration::ConfigurationIterator::operator++() {
  if (invalid) {
    throw InvalidIteratorException(
        "Attempting to use an iterator over a ConcordConfiguration that has "
        "been modified since the iterator's creation.");
  }
  if ((currentScope == endScopes) && (currentParam == endParams)) {
    throw std::out_of_range(
        "Attempting to advance an iterator already at the end of a "
        "ConcordConfiguration.");
  }

  bool hasVal = false;

  while (!hasVal &&
         ((currentScope != endScopes) || (currentParam != endParams))) {
    // Case where we have parameters we can return in the top-level scope.
    if (currentParam != endParams) {
      ++currentParam;
      hasVal = (currentParam != endParams) ||
               ((currentScope != endScopes) && (scopes && templates));

    } else if (currentScope != endScopes) {
      // Case where we continue iteration through a sub-scope of the
      // configuration by advancing a sub-iterator.
      if (currentScopeContents && endCurrentScope && recursive &&
          (*currentScopeContents != *endCurrentScope)) {
        ++(*currentScopeContents);
        hasVal = *currentScopeContents != *endCurrentScope;

        // Case where we have completed any non-recursive handling of a scope
        // itself and we procede to begin iterating through members of that
        // scope.
      } else if (recursive && (!currentScopeContents || !endCurrentScope) &&
                 ((templates && !usingInstance) ||
                  (instances && usingInstance))) {
        ConfigurationPath path = ConfigurationPath((*currentScope).first, true);
        if (usingInstance) {
          path.useInstance = true;
          path.index = instance;
        }
        ConcordConfiguration& scope = config->subscope(path);
        currentScopeContents.reset(new ConfigurationIterator(
            scope, recursive, scopes, parameters, instances, templates, false));
        endCurrentScope.reset(new ConfigurationIterator(
            scope, recursive, scopes, parameters, instances, templates, true));
        hasVal = currentScopeContents && endCurrentScope &&
                 (*currentScopeContents != *endCurrentScope);

        // Case where we have completed handling a specific instance of a scope
        // and we advance to the next instance.
      } else if (instances && usingInstance &&
                 (instance < ((*currentScope).second.instances.size() - 1))) {
        ++instance;
        currentScopeContents.reset();
        endCurrentScope.reset();
        hasVal = scopes;

        // Case where we have completed handling all relevant contents of a
        // specific scope and can move to the next scope (if any).
      } else if (!instances || (currentScope->second.instances.size() < 1) ||
                 (usingInstance &&
                  (instance >= (currentScope->second.instances.size() - 1)))) {
        usingInstance = false;
        instance = 0;
        currentScopeContents.reset();
        endCurrentScope.reset();
        ++currentScope;
        usingInstance = false;
        if (currentScope == endScopes) {
          hasVal = (currentParam != endParams);
        } else {
          hasVal = scopes && templates;
        }

        // Case where we have completed any template handling for a specific
        // scope and should begin handling instances of that scope.
      } else if (instances && !usingInstance &&
                 (currentScope->second.instances.size() > 0)) {
        usingInstance = true;
        instance = 0;
        currentScopeContents.reset();
        endCurrentScope.reset();
        hasVal = ((*currentScope).second.instances.size() > instance) && scopes;

        // The following cases should not be reached unless
        // ConfigurationIterator's implementation is buggy.
      } else {
        throw InvalidIteratorException(
            "ConcordConfiguration::ConfigurationIterator is implemented "
            "incorrectly: an iterator could not determine how to advance "
            "itself.");
      }

    } else {
      throw InvalidIteratorException(
          "ConcordConfiguration::ConfigurationIterator is implemented "
          "incorrectly: an iterator could not determine how to advance "
          "itself.");
    }
  }

  updateRetVal();

  return *this;
}

ConcordConfiguration::ConfigurationIterator
ConcordConfiguration::ConfigurationIterator::operator++(int) {
  ConfigurationIterator ret(*this);
  ++(*this);
  return ret;
}

void ConcordConfiguration::ConfigurationIterator::invalidate() {
  invalid = true;
}

void ConcordConfiguration::registerIterator(
    ConcordConfiguration::ConfigurationIterator* iterator) {
  iterators.insert(iterator);
}

void ConcordConfiguration::deregisterIterator(
    ConcordConfiguration::ConfigurationIterator* iterator) {
  iterators.erase(iterator);
}

ConcordConfiguration::ConcordConfiguration()
    : auxiliaryState(),
      configurationState(),
      parentScope(),
      scopePath(),
      scopes(),
      parameters(),
      iterators() {}

ConcordConfiguration::ConcordConfiguration(const ConcordConfiguration& original)
    : auxiliaryState(),
      configurationState(original.configurationState),
      parentScope(original.parentScope),
      scopePath(),
      scopes(original.scopes),
      parameters(original.parameters),
      iterators() {
  if (original.auxiliaryState) {
    auxiliaryState.reset(original.auxiliaryState->clone());
  }
  if (original.scopePath) {
    scopePath.reset(new ConfigurationPath(*(original.scopePath)));
  }
  for (auto& scopeEntry : scopes) {
    scopes[scopeEntry.first].instanceTemplate->parentScope = this;
    for (auto& instance : scopes[scopeEntry.first].instances) {
      instance.parentScope = this;
    }
  }
}

ConcordConfiguration::~ConcordConfiguration() {
  auxiliaryState.reset();
  invalidateIterators();
  scopes.clear();
  parameters.clear();
  configurationState = "";
}

ConcordConfiguration& ConcordConfiguration::operator=(
    const ConcordConfiguration& original) {
  invalidateIterators();

  configurationState = original.configurationState;
  parentScope = original.parentScope;
  if (original.auxiliaryState) {
    auxiliaryState.reset(original.auxiliaryState->clone());
  } else {
    auxiliaryState.reset();
  }
  if (original.scopePath) {
    scopePath.reset(new ConfigurationPath(*(original.scopePath)));
  } else {
    scopePath.reset();
  }
  parameters = original.parameters;
  scopes = original.scopes;

  for (auto& scopeEntry : scopes) {
    scopes[scopeEntry.first].instanceTemplate->parentScope = this;
    for (auto& instance : scopes[scopeEntry.first].instances) {
      instance.parentScope = this;
    }
  }
  return *this;
}

void ConcordConfiguration::clear() {
  configurationState = "";
  auxiliaryState.reset();
  invalidateIterators();
  scopes.clear();
  parameters.clear();
}

void ConcordConfiguration::setAuxiliaryState(
    ConfigurationAuxiliaryState* auxState) {
  auxiliaryState.reset(auxState);
}

ConfigurationAuxiliaryState* ConcordConfiguration::getAuxiliaryState() {
  return auxiliaryState.get();
}

const ConfigurationAuxiliaryState* ConcordConfiguration::getAuxiliaryState()
    const {
  return auxiliaryState.get();
}

void ConcordConfiguration::setConfigurationStateLabel(const string& state) {
  configurationState = state;
}

string ConcordConfiguration::getConfigurationStateLabel() const {
  return configurationState;
}

void ConcordConfiguration::declareScope(const string& scope,
                                        const string& description,
                                        ScopeSizer size, void* sizerState) {
  ConfigurationPath requestedScope(scope, true);
  if (scope.size() < 1) {
    throw invalid_argument(
        "Unable to create configuration scope: the empty string is not a valid "
        "name for a configuration scope.");
  }
  assert(kYAMLScopeTemplateSuffix.length() > 0);
  if ((scope.length() >= kYAMLScopeTemplateSuffix.length()) &&
      ((scope.substr(scope.length() - kYAMLScopeTemplateSuffix.length())) ==
       kYAMLScopeTemplateSuffix)) {
    throw invalid_argument("Cannot declare scope " + scope +
                           ": to facilitate configuration serialization, "
                           "scope names ending in \"" +
                           kYAMLScopeTemplateSuffix + "\" are disallowed.");
  }
  if (containsScope(scope)) {
    throw ConfigurationRedefinitionException(
        "Unable to create configuration scope " +
        printCompletePath(requestedScope) + ": scope already exists.");
  }
  if (contains(scope)) {
    throw ConfigurationRedefinitionException(
        "Unable to create configuration scope " +
        printCompletePath(requestedScope) + ": identifier " + scope +
        " is already used for a parameter.");
  }
  if (!size) {
    throw invalid_argument("Unable to create configuration scope " +
                           printCompletePath(requestedScope) +
                           ": provided scope sizer function is null.");
  }
  invalidateIterators();
  scopes[scope] = ConfigurationScope();
  scopes[scope].instanceTemplate.reset(new ConcordConfiguration());
  scopes[scope].instantiated = false;
  scopes[scope].description = description;
  scopes[scope].size = size;
  scopes[scope].sizerState = sizerState;

  scopes[scope].instanceTemplate->parentScope = this;
  ConfigurationPath relativeScopePath(scope, true);
  ConfigurationPath* path;
  if (scopePath) {
    path = new ConfigurationPath(scopePath->concatenate(relativeScopePath));
  } else {
    path = new ConfigurationPath(relativeScopePath);
  }
  scopes[scope].instanceTemplate->scopePath.reset(path);
}

string ConcordConfiguration::getScopeDescription(const string& scope) const {
  if (!containsScope(scope)) {
    ConfigurationPath path(scope, true);
    throw ConfigurationResourceNotFoundException(
        "Cannot get description for scope " + printCompletePath(path) +
        ": scope does not exist.");
  }
  return scopes.at(scope).description;
}

ConcordConfiguration::ParameterStatus ConcordConfiguration::instantiateScope(
    const string& scope) {
  ConfigurationPath relativePath(scope, true);
  if (!containsScope(scope)) {
    throw ConfigurationResourceNotFoundException(
        "Unable to instantiate configuration scope " +
        printCompletePath(relativePath) + ": scope does not exist.");
  }
  ConfigurationScope& scopeEntry = scopes[scope];
  if (!(scopeEntry.size)) {
    throw invalid_argument("Unable to instantiate configuration scope " +
                           printCompletePath(relativePath) +
                           ": scope does not have a size function.");
  }
  size_t scopeSize;
  std::unique_ptr<ConfigurationPath> fullPath(getCompletePath(relativePath));
  ParameterStatus result = scopeEntry.size(*(getRootConfig()), *fullPath,
                                           &scopeSize, scopeEntry.sizerState);
  if (result != ParameterStatus::VALID) {
    return result;
  }

  invalidateIterators();
  scopeEntry.instantiated = true;
  scopeEntry.instances.clear();
  for (size_t i = 0; i < scopeSize; ++i) {
    scopeEntry.instances.push_back(
        ConcordConfiguration(*(scopeEntry.instanceTemplate)));
    ConfigurationPath instancePath(scope, i);
    scopeEntry.instances[i].scopePath.reset(getCompletePath(instancePath));
    scopeEntry.instances[i].updateSubscopePaths();
  }
  scopeEntry.instantiated = true;
  return result;
}

ConcordConfiguration& ConcordConfiguration::subscope(const string& scope) {
  // This cast avoids duplicating code between the const and non-const versions
  // of this function.
  return const_cast<ConcordConfiguration&>(
      (const_cast<const ConcordConfiguration*>(this))->subscope(scope));
}

const ConcordConfiguration& ConcordConfiguration::subscope(
    const string& scope) const {
  if (!containsScope(scope)) {
    ConfigurationPath path(scope, true);
    throw ConfigurationResourceNotFoundException("Could not find scope " +
                                                 printCompletePath(path) + ".");
  }
  return *(scopes.at(scope).instanceTemplate);
}

ConcordConfiguration& ConcordConfiguration::subscope(const string& scope,
                                                     size_t index) {
  // This cast avoids duplicating code between the const and non-const versions
  // of this function.
  return const_cast<ConcordConfiguration&>(
      (const_cast<const ConcordConfiguration*>(this))->subscope(scope, index));
}

const ConcordConfiguration& ConcordConfiguration::subscope(const string& scope,
                                                           size_t index) const {
  if ((scopes.count(scope) < 1) || (!(scopes.at(scope).instantiated)) ||
      (index >= scopes.at(scope).instances.size())) {
    ConfigurationPath path(scope, index);
    throw ConfigurationResourceNotFoundException("Could not find scope " +
                                                 printCompletePath(path) + ".");
  }
  return scopes.at(scope).instances[index];
}

ConcordConfiguration& ConcordConfiguration::subscope(
    const ConfigurationPath& path) {
  // This cast avoids duplicating code between the const and non-const versions
  // of this function.
  return const_cast<ConcordConfiguration&>(
      (const_cast<const ConcordConfiguration*>(this))->subscope(path));
}

const ConcordConfiguration& ConcordConfiguration::subscope(
    const ConfigurationPath& path) const {
  if (!path.isScope || (scopes.count(path.name) < 1) ||
      (path.useInstance &&
       ((!(scopes.at(path.name).instantiated)) ||
        (path.index >= scopes.at(path.name).instances.size())))) {
    throw ConfigurationResourceNotFoundException("Could not find scope " +
                                                 printCompletePath(path) + ".");
  }

  const ConcordConfiguration* subscope =
      scopes.at(path.name).instanceTemplate.get();
  if (path.useInstance) {
    subscope = &(scopes.at(path.name).instances[path.index]);
  }
  if (path.subpath) {
    return subscope->subscope(*(path.subpath));
  } else {
    return *subscope;
  }
}

bool ConcordConfiguration::containsScope(const string& name) const {
  return (scopes.count(name) > 0);
}

bool ConcordConfiguration::containsScope(const ConfigurationPath& path) const {
  if (!path.isScope || (scopes.count(path.name) < 1) ||
      (path.useInstance &&
       ((!(scopes.at(path.name).instantiated)) ||
        (path.index >= scopes.at(path.name).instances.size())))) {
    return false;
  }
  if (path.subpath) {
    ConcordConfiguration& subscope = *(scopes.at(path.name).instanceTemplate);
    if (path.useInstance) {
      subscope = scopes.at(path.name).instances[path.index];
    }
    return subscope.containsScope(*(path.subpath));
  } else {
    return true;
  }
}

bool ConcordConfiguration::scopeIsInstantiated(const string& name) const {
  return containsScope(name) && scopes.at(name).instantiated;
}

size_t ConcordConfiguration::scopeSize(const string& scope) const {
  if (!scopeIsInstantiated(scope)) {
    ConfigurationPath path(scope, true);
    throw ConfigurationResourceNotFoundException("Cannot get size of scope " +
                                                 printCompletePath(path) +
                                                 ": scope does not exist.");
  }
  return scopes.at(scope).instances.size();
}

void ConcordConfiguration::declareParameter(const string& name,
                                            const string& description) {
  if (name.size() < 1) {
    throw invalid_argument(
        "Cannot declare parameter: the empty string is not a valid name for a "
        "configuration parameter.");
  }
  assert(kYAMLScopeTemplateSuffix.length() > 0);
  if ((name.length() >= kYAMLScopeTemplateSuffix.length()) &&
      ((name.substr(name.length() - kYAMLScopeTemplateSuffix.length())) ==
       kYAMLScopeTemplateSuffix)) {
    throw invalid_argument("Cannot declare parameter " + name +
                           ": to facilitate configuration serialization, "
                           "parameter names ending in \"" +
                           kYAMLScopeTemplateSuffix + "\" are disallowed.");
  }
  if (contains(name)) {
    ConfigurationPath path(name, false);
    throw ConfigurationRedefinitionException("Cannot declare parameter " +
                                             printCompletePath(path) +
                                             ": parameter already exists.");
  }
  if (containsScope(name)) {
    ConfigurationPath path(name, false);
    throw ConfigurationRedefinitionException(
        "Cannot declare parameter " + printCompletePath(path) +
        ": identifier is already used for a scope.");
  }

  invalidateIterators();
  parameters[name] = ConfigurationParameter();
  ConfigurationParameter& parameter = parameters[name];
  parameter.description = description;
  parameter.hasDefaultValue = false;
  parameter.defaultValue = "";
  parameter.initialized = false;
  parameter.value = "";
  parameter.tags = std::unordered_set<string>();
  parameter.validator = nullptr;
  parameter.validatorState = nullptr;
  parameter.generator = nullptr;
  parameter.generatorState = nullptr;
}

void ConcordConfiguration::declareParameter(const string& name,
                                            const string& description,
                                            const string& defaultValue) {
  declareParameter(name, description);
  ConfigurationParameter& parameter = parameters[name];
  parameter.defaultValue = defaultValue;
  parameter.hasDefaultValue = true;
}

void ConcordConfiguration::tagParameter(const string& name,
                                        const vector<string>& tags) {
  ConfigurationParameter& parameter =
      getParameter(name, "Cannot tag parameter ");
  for (auto&& tag : tags) {
    parameter.tags.emplace(tag);
  }
}

bool ConcordConfiguration::isTagged(const string& name,
                                    const string& tag) const {
  const ConfigurationParameter& parameter =
      getParameter(name, "Cannot check tags for parameter ");
  return parameter.tags.count(tag) > 0;
}

string ConcordConfiguration::getDescription(const string& name) const {
  const ConfigurationParameter& parameter =
      getParameter(name, "Cannot get description for parameter ");
  return parameter.description;
}

void ConcordConfiguration::addValidator(const string& name, Validator validator,
                                        void* validatorState) {
  ConfigurationParameter& parameter =
      getParameter(name, "Cannot add validator to parameter ");
  if (!validator) {
    throw invalid_argument("Cannot add validator to parameter " +
                           printCompletePath(ConfigurationPath(name, false)) +
                           ": validator given points to null.");
  }
  parameter.validator = validator;
  parameter.validatorState = validatorState;
}

void ConcordConfiguration::addGenerator(const string& name, Generator generator,
                                        void* generatorState) {
  ConfigurationParameter& parameter =
      getParameter(name, "Cannot add generator to parameter ");
  if (!generator) {
    throw invalid_argument("Cannot add generator to parameter " +
                           printCompletePath(ConfigurationPath(name, false)) +
                           ": generator given points to null.");
  }
  parameter.generator = generator;
  parameter.generatorState = generatorState;
}

bool ConcordConfiguration::contains(const string& name) const {
  return parameters.count(name) > 0;
}

bool ConcordConfiguration::contains(const ConfigurationPath& path) const {
  if (path.isScope && path.subpath) {
    if (scopes.count(path.name) < 1) {
      return false;
    }
    const ConfigurationScope& scope = scopes.at(path.name);
    const ConcordConfiguration* subscope = scope.instanceTemplate.get();
    if (path.useInstance) {
      if (!(scope.instantiated) || (path.index >= scope.instances.size())) {
        return false;
      } else {
        subscope = &(scope.instances[path.index]);
      }
    }
    return subscope->contains(*(path.subpath));
  } else {
    return !(path.isScope) && contains(path.name);
  }
}

ConcordConfiguration::ParameterStatus ConcordConfiguration::loadValue(
    const string& name, const string& value, string* failureMessage,
    bool overwrite, string* prevValue) {
  ConfigurationParameter& parameter =
      getParameter(name, "Could not load value for parameter ");
  std::unique_ptr<ConfigurationPath> path(
      getCompletePath(ConfigurationPath(name, false)));
  ParameterStatus status = ParameterStatus::VALID;
  string message;
  if (parameter.validator) {
    status = parameter.validator(value, *(getRootConfig()), *path, &message,
                                 parameter.validatorState);
  }
  if (failureMessage && (status != ParameterStatus::VALID)) {
    *failureMessage = message;
  }

  if (status != ParameterStatus::INVALID) {
    if (parameter.initialized) {
      if (overwrite) {
        if (prevValue) {
          *prevValue = parameter.value;
        }
        parameter.value = value;
      }
    } else {
      parameter.value = value;
      parameter.initialized = true;
    }
  }

  return status;
}

void ConcordConfiguration::eraseValue(const string& name, string* prevValue) {
  ConfigurationParameter& parameter =
      getParameter(name, "Could not erase value for parameter ");
  if (prevValue && parameter.initialized) {
    *prevValue = parameter.value;
  }
  parameter.value = "";
  parameter.initialized = false;
}

void ConcordConfiguration::eraseAllValues() {
  auto iterator = this->begin(kIterateAllParameters);
  auto end = this->end(kIterateAllParameters);
  while (iterator != end) {
    const ConfigurationPath& path = *iterator;
    ConcordConfiguration* containingScope = this;
    if (path.isScope && path.subpath) {
      containingScope = &(subscope(path.trimLeaf()));
    }
    containingScope->eraseValue(path.getLeaf().name);
    ++iterator;
  }
}

ConcordConfiguration::ParameterStatus ConcordConfiguration::loadDefault(
    const string& name, string* failureMessage, bool overwrite,
    string* prevValue) {
  ConfigurationParameter& parameter =
      getParameter(name, "Could not load default value for parameter ");

  if (!parameter.hasDefaultValue) {
    throw ConfigurationResourceNotFoundException(
        "Could not load default value for parameter " +
        printCompletePath(name) +
        ": this parameter does not have a default value.");
  }
  return loadValue(name, parameter.defaultValue, failureMessage, overwrite,
                   prevValue);
}

ConcordConfiguration::ParameterStatus ConcordConfiguration::loadAllDefaults(
    bool overwrite, bool includeTemplates) {
  ParameterStatus status = ParameterStatus::VALID;

  IteratorFeatureSelection iteratorFeatures = kIterateAllInstanceParameters;
  if (includeTemplates) {
    iteratorFeatures |= kTraverseTemplates;
  }
  auto iterator = this->begin(iteratorFeatures);
  auto end = this->end(iteratorFeatures);
  while (iterator != end) {
    const ConfigurationPath& path = *iterator;
    ConcordConfiguration* containingScope = this;
    if (path.isScope && path.subpath) {
      containingScope = &(subscope(path.trimLeaf()));
    }
    if (containingScope->parameters[path.getLeaf().name].hasDefaultValue) {
      ParameterStatus loadRes =
          containingScope->loadDefault(path.getLeaf().name, nullptr, overwrite);
      if ((loadRes == ParameterStatus::INVALID) ||
          (status == ParameterStatus::VALID)) {
        status = loadRes;
      }
    }
    ++iterator;
  }
  return status;
}

ConcordConfiguration::ParameterStatus ConcordConfiguration::validate(
    const string& name, string* failureMessage) const {
  const ConfigurationParameter& parameter =
      getParameter(name, "Could not validate contents of parameter ");
  std::unique_ptr<ConfigurationPath> path(
      getCompletePath(ConfigurationPath(name, false)));
  if (!parameter.initialized) {
    return ParameterStatus::INSUFFICIENT_INFORMATION;
  }
  ParameterStatus status = ParameterStatus::VALID;
  string message;
  if (parameter.validator) {
    status = parameter.validator(parameter.value, *(getRootConfig()), *path,
                                 &message, parameter.validatorState);
  }
  if (failureMessage && (status != ParameterStatus::VALID)) {
    *failureMessage = message;
  }
  return status;
}

ConcordConfiguration::ParameterStatus ConcordConfiguration::validateAll(
    bool ignoreUninitializedParameters, bool includeTemplates) {
  ParameterStatus status = ParameterStatus::VALID;

  IteratorFeatureSelection iteratorFeatures = kIterateAllInstanceParameters;
  if (includeTemplates) {
    iteratorFeatures |= kTraverseTemplates;
  }
  auto iterator = this->begin(iteratorFeatures);
  auto end = this->end(iteratorFeatures);
  while (iterator != end) {
    const ConfigurationPath& path = *iterator;
    ConcordConfiguration* containingScope = this;
    if (path.isScope && path.subpath) {
      containingScope = &(subscope(path.trimLeaf()));
    }

    // We do not give an error message to getParameter in this case because we
    // do not expect getParameter to fail because the iterator over this should
    // not return paths that are not to existing parameters.
    const ConfigurationParameter& parameter =
        containingScope->getParameter(path.getLeaf().name, "");

    if (parameter.initialized) {
      ParameterStatus validateRes =
          containingScope->validate(path.getLeaf().name);
      if ((validateRes == ParameterStatus::INVALID) ||
          (status == ParameterStatus::VALID)) {
        status = validateRes;
      }
    } else if (!ignoreUninitializedParameters) {
      if (status != ParameterStatus::INVALID) {
        status = ParameterStatus::INSUFFICIENT_INFORMATION;
      }
    }
    ++iterator;
  }
  return status;
}

ConcordConfiguration::ParameterStatus ConcordConfiguration::generate(
    const string& name, string* failureMessage, bool overwrite,
    string* prevValue) {
  ConfigurationParameter& parameter =
      getParameter(name, "Cannot generate value for parameter ");
  std::unique_ptr<ConfigurationPath> path(
      getCompletePath(ConfigurationPath(name, false)));

  if (!parameter.generator) {
    throw ConfigurationResourceNotFoundException(
        "Cannot generate value for parameter " + printCompletePath(*path) +
        ": no generator function has been specified for this parameter.");
  }
  string generatedValue;
  ParameterStatus status = parameter.generator(
      *(getRootConfig()), *path, &generatedValue, parameter.generatorState);
  if (status == ParameterStatus::VALID) {
    string message;
    if (parameter.validator) {
      status = parameter.validator(generatedValue, *(getRootConfig()), *path,
                                   &message, parameter.validatorState);
    }
    if (failureMessage && (status != ParameterStatus::VALID)) {
      *failureMessage = message;
    }
    if ((status != ParameterStatus::INVALID) &&
        (!parameter.initialized || overwrite)) {
      if (prevValue && parameter.initialized) {
        *prevValue = parameter.value;
      }
      parameter.value = generatedValue;
      parameter.initialized = true;
    }
  }
  return status;
}

ConcordConfiguration::ParameterStatus ConcordConfiguration::generateAll(
    bool overwrite, bool includeTemplates) {
  ParameterStatus status = ParameterStatus::VALID;

  IteratorFeatureSelection iteratorFeatures = kIterateAllInstanceParameters;
  if (includeTemplates) {
    iteratorFeatures |= kTraverseTemplates;
  }
  auto iterator = this->begin(iteratorFeatures);
  auto end = this->end(iteratorFeatures);
  while (iterator != end) {
    const ConfigurationPath& path = *iterator;
    ConcordConfiguration* containingScope = this;
    if (path.isScope && path.subpath) {
      containingScope = &(subscope(path.trimLeaf()));
    }

    // Note we do not provide an error message here to getParameter because
    // getParameter should not fail to find this parameter, given this request
    // is based on a path we got by iterating through this ConcordConfiguration.
    ConfigurationParameter& parameter =
        containingScope->getParameter(path.getLeaf().name, "");

    if (parameter.generator) {
      ParameterStatus generateRes =
          containingScope->generate(path.getLeaf().name, nullptr, overwrite);
      if ((generateRes == ParameterStatus::INVALID) ||
          (status == ParameterStatus::VALID)) {
        status = generateRes;
      }
    }
    ++iterator;
  }
  return status;
}

ConcordConfiguration::Iterator ConcordConfiguration::begin(
    ConcordConfiguration::IteratorFeatureSelection features) {
  ConfigurationIterator it(
      *this, (features & kTraverseRecursively), (features & kTraverseScopes),
      (features & kTraverseParameters), (features & kTraverseInstances),
      (features & kTraverseTemplates), false);
  return it;
}

ConcordConfiguration::Iterator ConcordConfiguration::end(
    ConcordConfiguration::IteratorFeatureSelection features) {
  ConfigurationIterator it(
      *this, (features & kTraverseRecursively), (features & kTraverseScopes),
      (features & kTraverseParameters), (features & kTraverseInstances),
      (features & kTraverseTemplates), true);
  return it;
}

ParameterSelection::ParameterSelectionIterator::ParameterSelectionIterator()
    : selection(nullptr),
      unfilteredIterator(),
      endUnfilteredIterator(),
      invalid(false) {}

ParameterSelection::ParameterSelectionIterator::ParameterSelectionIterator(
    ParameterSelection* selection, bool end)
    : selection(selection), invalid(false) {
  if (selection) {
    endUnfilteredIterator =
        selection->config->end(ConcordConfiguration::kIterateAllParameters);
    if (end) {
      unfilteredIterator =
          selection->config->end(ConcordConfiguration::kIterateAllParameters);
    } else {
      unfilteredIterator =
          selection->config->begin(ConcordConfiguration::kIterateAllParameters);
    }

    // Advance from the first value to the first value this iterator should
    // actually return if the first value the unfiltered iterator has is not
    // actually in the selection.
    if ((unfilteredIterator != endUnfilteredIterator) &&
        (!(selection->contains(*unfilteredIterator)))) {
      ++(*this);
    }

    selection->registerIterator(this);
  }
}

ParameterSelection::ParameterSelectionIterator::ParameterSelectionIterator(
    const ParameterSelection::ParameterSelectionIterator& original)
    : selection(original.selection),
      unfilteredIterator(original.unfilteredIterator),
      endUnfilteredIterator(original.endUnfilteredIterator),
      invalid(original.invalid) {
  if (selection && !invalid) {
    selection->registerIterator(this);
  }
}

ParameterSelection::ParameterSelectionIterator::~ParameterSelectionIterator() {
  if (selection && !invalid) {
    selection->deregisterIterator(this);
  }
}

ParameterSelection::ParameterSelectionIterator&
ParameterSelection::ParameterSelectionIterator::operator=(
    const ParameterSelection::ParameterSelectionIterator& original) {
  if (selection && !invalid) {
    selection->deregisterIterator(this);
  }
  selection = original.selection;
  unfilteredIterator = original.unfilteredIterator;
  endUnfilteredIterator = original.unfilteredIterator;
  invalid = original.invalid;
  if (selection && !invalid) {
    selection->registerIterator(this);
  }
  return *this;
}

bool ParameterSelection::ParameterSelectionIterator::operator==(
    const ParameterSelection::ParameterSelectionIterator& other) const {
  return (selection == other.selection) &&
         (unfilteredIterator == other.unfilteredIterator) &&
         (endUnfilteredIterator == other.endUnfilteredIterator) &&
         (invalid == other.invalid);
}

bool ParameterSelection::ParameterSelectionIterator::operator!=(
    const ParameterSelection::ParameterSelectionIterator& other) const {
  return !((*this) == other);
}

const ConfigurationPath& ParameterSelection::ParameterSelectionIterator::
operator*() const {
  if (invalid) {
    throw InvalidIteratorException(
        "Attempting to use an iterator over a ParameterSelection that has been "
        "modified since the iterator's creation.");
  }
  if (!selection || (unfilteredIterator == endUnfilteredIterator)) {
    throw std::out_of_range(
        "Attempting to get the value at an iterator already at the end of a "
        "ParameterSelection.");
  }
  return *unfilteredIterator;
}

ParameterSelection::ParameterSelectionIterator&
ParameterSelection::ParameterSelectionIterator::operator++() {
  if (invalid) {
    throw InvalidIteratorException(
        "Attempting to use an iterator over a ParameterSelection that has been "
        "modified since the iterator's creation.");
  }
  if (!selection || (unfilteredIterator == endUnfilteredIterator)) {
    throw std::out_of_range(
        "Attempting to advance an iterator already at the end of a "
        "ParameterSelection.");
  }
  ++unfilteredIterator;
  while ((unfilteredIterator != endUnfilteredIterator) &&
         (!(selection->contains(*unfilteredIterator)))) {
    ++unfilteredIterator;
  }
  return *this;
}

ParameterSelection::ParameterSelectionIterator
ParameterSelection::ParameterSelectionIterator::operator++(int) {
  ParameterSelectionIterator ret(*this);
  ++(*this);
  return ret;
}

void ParameterSelection::ParameterSelectionIterator::invalidate() {
  invalid = true;
}

void ParameterSelection::registerIterator(
    ParameterSelection::ParameterSelectionIterator* iterator) {
  iterators.insert(iterator);
}

void ParameterSelection::deregisterIterator(
    ParameterSelection::ParameterSelectionIterator* iterator) {
  iterators.erase(iterator);
}

void ParameterSelection::invalidateIterators() {
  for (ParameterSelection::Iterator* iterator : iterators) {
    iterator->invalidate();
  }
  iterators.clear();
}

ParameterSelection::ParameterSelection(ConcordConfiguration& config,
                                       ParameterSelector selector,
                                       void* selectorState)
    : config(&config),
      selector(selector),
      selectorState(selectorState),
      iterators() {
  if (!selector) {
    throw invalid_argument(
        "Attempting to construct a ParameterSelection with a null parameter "
        "selction function.");
  }
}

ParameterSelection::ParameterSelection(const ParameterSelection& original)
    : config(original.config),
      selector(original.selector),
      selectorState(original.selectorState) {}

ParameterSelection::~ParameterSelection() { invalidateIterators(); }

bool ParameterSelection::contains(const ConfigurationPath& parameter) const {
  return (config->contains(parameter)) &&
         (selector(*config, parameter, selectorState));
}

ParameterSelection::Iterator ParameterSelection::begin() {
  return ParameterSelectionIterator(this, false);
}

ParameterSelection::Iterator ParameterSelection::end() {
  return ParameterSelectionIterator(this, true);
}

void YAMLConfigurationInput::loadParameter(ConcordConfiguration& config,
                                           const ConfigurationPath& path,
                                           const YAML::Node& obj,
                                           Logger* errorOut, bool overwrite) {
  // Note cases in this function where we return without either writing a value
  // to the configuration or making a recursive call indicate we have concluded
  // that the parameter indicated by path is not given in the input.
  if (!obj.IsMap()) {
    return;
  }

  if (path.isScope && path.subpath) {
    YAML::Node subObj;
    if (path.useInstance) {
      if (!obj[path.name]) {
        return;
      }
      subObj.reset(obj[path.name]);
      if (!subObj.IsSequence() || (path.index >= subObj.size())) {
        return;
      }
      subObj.reset(subObj[path.index]);
    } else {
      string templateName = path.name + kYAMLScopeTemplateSuffix;
      if (!obj[templateName]) {
        return;
      }
      subObj.reset(obj[templateName]);
    }
    ConfigurationPath subscope(path);
    subscope.subpath.reset();
    loadParameter(config.subscope(subscope), *(path.subpath), subObj, errorOut,
                  overwrite);

  } else {
    if (!obj[path.name] || !obj[path.name].IsScalar()) {
      return;
    }

    string failureMessage;
    ConcordConfiguration::ParameterStatus status = config.loadValue(
        path.name, obj[path.name].Scalar(), &failureMessage, overwrite);
    if (errorOut &&
        (status == ConcordConfiguration::ParameterStatus::INVALID)) {
      LOG4CPLUS_ERROR((*errorOut), "Cannot load value for parameter " +
                                       path.name + ": " + failureMessage);
    }
  }
}

YAMLConfigurationInput::YAMLConfigurationInput(std::istream& input)
    : input(&input), yaml(), success(false) {}

void YAMLConfigurationInput::parseInput() {
  yaml.reset(YAML::Load(*input));
  success = true;
}

YAMLConfigurationInput::~YAMLConfigurationInput() {}

void YAMLConfigurationOutput::addParameterToYAML(
    const ConcordConfiguration& config, const ConfigurationPath& path,
    YAML::Node& yaml) {
  // Note this helper function expects that it has already been validated or
  // otherwise guaranteed that path is a valid path to a declared parameter in
  // config and yaml is an associative array.
  if (!config.contains(path) || !config.hasValue<string>(path) ||
      !yaml.IsMap()) {
    return;
  }

  if (path.isScope && path.subpath) {
    YAML::Node subscope;
    string pathName;
    if (path.useInstance) {
      pathName = path.name;
      if (!yaml[pathName]) {
        yaml[pathName] = YAML::Node(YAML::NodeType::Sequence);
      }
      subscope.reset(yaml[pathName]);
      assert(subscope.IsSequence());
      while (path.index >= subscope.size()) {
        subscope.push_back(YAML::Node(YAML::NodeType::Map));
      }
      subscope.reset(subscope[path.index]);
    } else {
      pathName = path.name + kYAMLScopeTemplateSuffix;
      if (!yaml[pathName]) {
        yaml[pathName] = YAML::Node(YAML::NodeType::Map);
      }
      subscope.reset(yaml[pathName]);
      assert(subscope.IsMap());
    }
    ConfigurationPath subscopePath(path);
    subscopePath.subpath.reset();
    addParameterToYAML(config.subscope(subscopePath), *(path.subpath),
                       subscope);
  } else {
    yaml[path.name] = config.getValue<string>(path.name);
  }
}

YAMLConfigurationOutput::YAMLConfigurationOutput(std::ostream& output)
    : output(&output), yaml() {}

YAMLConfigurationOutput::~YAMLConfigurationOutput() {}

ConcordPrimaryConfigurationAuxiliaryState::
    ConcordPrimaryConfigurationAuxiliaryState()
    : slowCommitCryptosys(), commitCryptosys(), optimisticCommitCryptosys() {}

ConcordPrimaryConfigurationAuxiliaryState::
    ~ConcordPrimaryConfigurationAuxiliaryState() {
  slowCommitCryptosys.reset();
  commitCryptosys.reset();
  optimisticCommitCryptosys.reset();
};

ConfigurationAuxiliaryState*
ConcordPrimaryConfigurationAuxiliaryState::clone() {
  ConcordPrimaryConfigurationAuxiliaryState* copy =
      new ConcordPrimaryConfigurationAuxiliaryState();
  if (slowCommitCryptosys) {
    copy->slowCommitCryptosys.reset(new Cryptosystem(*slowCommitCryptosys));
  }
  if (commitCryptosys) {
    copy->commitCryptosys.reset(new Cryptosystem(*commitCryptosys));
  }
  if (optimisticCommitCryptosys) {
    copy->optimisticCommitCryptosys.reset(
        new Cryptosystem(*optimisticCommitCryptosys));
  }
  return copy;
}

// generateRSAKeyPair implementation, which itself just uses an implementation
// for RSA key generation from CryptoPP.
std::pair<string, string> generateRSAKeyPair(
    CryptoPP::RandomPool& randomnessSource) {
  std::pair<string, string> keyPair;

  CryptoPP::RSAES<CryptoPP::OAEP<CryptoPP::SHA256>>::Decryptor privateKey(
      randomnessSource, kRSAKeyLength);
  CryptoPP::HexEncoder privateEncoder(new CryptoPP::StringSink(keyPair.first));
  privateKey.AccessMaterial().Save(privateEncoder);
  privateEncoder.MessageEnd();

  CryptoPP::RSAES<CryptoPP::OAEP<CryptoPP::SHA256>>::Encryptor publicKey(
      privateKey);
  CryptoPP::HexEncoder publicEncoder(new CryptoPP::StringSink(keyPair.second));
  publicKey.AccessMaterial().Save(publicEncoder);
  publicEncoder.MessageEnd();

  return keyPair;
}

// Helper functions to the validation, generation, and sizing functions to
// follow.

static ConcordConfiguration::ParameterStatus validateNumberOfPrincipalsInBounds(
    uint16_t fVal, uint16_t cVal, uint16_t clientProxiesPerReplica,
    string* failureMessage) {
  // Conditions which should have been validated before this function was
  // called.
#ifdef ALLOW_0_FAULT_TOLERANCE
  assert((fVal >= 0) && (clientProxiesPerReplica > 0));
#else
  assert((fVal > 0) && (clientProxiesPerReplica > 0));
#endif
  // The principals consist of (3F + 2C + 1) SBFT replicas plus
  // client_proxies_per_replica client proxies for each of these replicas.
  uint64_t numPrincipals = 3 * ((uint64_t)fVal) + 2 * ((uint64_t)cVal) + 1;
  numPrincipals =
      numPrincipals + ((uint64_t)clientProxiesPerReplica) * numPrincipals;

  if (numPrincipals > UINT16_MAX) {
    if (failureMessage) {
      *failureMessage =
          "Invalid combination of values for f_val (" + to_string(fVal) +
          "), c_val (" + to_string(cVal) +
          "), and client_proxies_per_replica (" +
          to_string(clientProxiesPerReplica) +
          "): these parameters imply too many (" + to_string(numPrincipals) +
          ") SBFT principals; a maximum of " + to_string(UINT16_MAX) +
          " principals are currently supported.";
    }
    return ConcordConfiguration::ParameterStatus::INVALID;
  }
  return ConcordConfiguration::ParameterStatus::VALID;
}

static std::pair<string, string> parseCryptosystemSelection(string selection) {
  std::pair<string, string> parseRes({"", ""});
  boost::trim(selection);
  size_t spaceLoc = selection.find_first_of(" ");
  parseRes.first = selection.substr(0, spaceLoc);
  if (spaceLoc < selection.length()) {
    parseRes.second = selection.substr(spaceLoc);
  }
  boost::trim(parseRes.first);
  boost::trim(parseRes.second);
  return parseRes;
}

ConcordConfiguration::ParameterStatus sizeNodes(
    const ConcordConfiguration& config, const ConfigurationPath& path,
    size_t* output, void* state) {
  assert(output);

  if (!(config.hasValue<uint16_t>("f_val") &&
        config.hasValue<uint16_t>("c_val"))) {
    return ConcordConfiguration::ParameterStatus::INSUFFICIENT_INFORMATION;
  }
  if (!((config.validate("f_val") ==
         ConcordConfiguration::ParameterStatus::VALID) &&
        (config.validate("c_val") ==
         ConcordConfiguration::ParameterStatus::VALID))) {
    return ConcordConfiguration::ParameterStatus::INVALID;
  }

  uint16_t f = config.getValue<uint16_t>("f_val");
  uint16_t c = config.getValue<uint16_t>("c_val");
  size_t numNodes = 3 * (size_t)f + 2 * (size_t)c + 1;
  if (numNodes > (size_t)UINT16_MAX) {
    return ConcordConfiguration::ParameterStatus::INVALID;
  }
  *output = numNodes;
  return ConcordConfiguration::ParameterStatus::VALID;
}

ConcordConfiguration::ParameterStatus sizeReplicas(
    const ConcordConfiguration& config, const ConfigurationPath& path,
    size_t* output, void* state) {
  assert(output);

  *output = 1;
  return ConcordConfiguration::ParameterStatus::VALID;
}

static ConcordConfiguration::ParameterStatus sizeClientProxies(
    const ConcordConfiguration& config, const ConfigurationPath& path,
    size_t* output, void* state) {
  assert(output);

  if (!config.hasValue<uint16_t>("client_proxies_per_replica")) {
    return ConcordConfiguration::ParameterStatus::INSUFFICIENT_INFORMATION;
  }
  if (config.validate("client_proxies_per_replica") !=
      ConcordConfiguration::ParameterStatus::VALID) {
    return ConcordConfiguration::ParameterStatus::INVALID;
  }

  *output = config.getValue<uint16_t>("client_proxies_per_replica");
  return ConcordConfiguration::ParameterStatus::VALID;
}

static ConcordConfiguration::ParameterStatus validateBoolean(
    const string& value, const ConcordConfiguration& config,
    const ConfigurationPath& path, string* failureMessage, void* state) {
  if (std::find(kValidBooleansTrue.begin(), kValidBooleansTrue.end(), value) !=
          kValidBooleansTrue.end() ||
      std::find(kValidBooleansFalse.begin(), kValidBooleansFalse.end(),
                value) != kValidBooleansFalse.end()) {
    return ConcordConfiguration::ParameterStatus::VALID;
  }

  if (failureMessage) {
    *failureMessage = "Invalid value for parameter " + path.toString() +
                      ": \"" + value +
                      "\". A boolean (e.g. \"true\" or \"false\") is required.";
  }
  return ConcordConfiguration::ParameterStatus::INVALID;
}

static ConcordConfiguration::ParameterStatus validateInt(
    const string& value, const ConcordConfiguration& config,
    const ConfigurationPath& path, string* failureMessage, void* state) {
  assert(state);
  const std::pair<long long, long long>* limits =
      static_cast<std::pair<long long, long long>*>(state);
  assert(limits->first <= limits->second);

  long long intVal;
  try {
    intVal = std::stoll(value);
  } catch (invalid_argument& e) {
    if (failureMessage) {
      *failureMessage = "Invalid value for parameter " + path.toString() +
                        ": \"" + value + "\". An integer is required.";
    }
    return ConcordConfiguration::ParameterStatus::INVALID;
  } catch (std::out_of_range& e) {
    if (failureMessage) {
      *failureMessage =
          "Invalid value for parameter " + path.toString() + ": \"" + value +
          "\". An integer in the range (" + to_string(limits->first) + ", " +
          to_string(limits->second) + "), inclusive, is required.";
    }
    return ConcordConfiguration::ParameterStatus::INVALID;
  }
  if ((intVal < limits->first) || (intVal > limits->second)) {
    if (failureMessage) {
      *failureMessage =
          "Invalid value for parameter " + path.toString() + ": \"" + value +
          "\". An integer in the range (" + to_string(limits->first) + ", " +
          to_string(limits->second) + "), inclusive, is required.";
    }
    return ConcordConfiguration::ParameterStatus::INVALID;
  }
  return ConcordConfiguration::ParameterStatus::VALID;
}

ConcordConfiguration::ParameterStatus validateUInt(
    const string& value, const ConcordConfiguration& config,
    const ConfigurationPath& path, string* failureMessage, void* state) {
  assert(state);
  const std::pair<unsigned long long, unsigned long long>* limits =
      static_cast<std::pair<unsigned long long, unsigned long long>*>(state);
  assert(limits->first <= limits->second);

  unsigned long long intVal;
  try {
    intVal = std::stoull(value);
  } catch (invalid_argument& e) {
    if (failureMessage) {
      *failureMessage = "Invalid value for parameter " + path.toString() +
                        ": \"" + value + "\". An integer is required.";
    }
    return ConcordConfiguration::ParameterStatus::INVALID;
  } catch (std::out_of_range& e) {
    if (failureMessage) {
      *failureMessage =
          "Invalid value for parameter " + path.toString() + ": \"" + value +
          "\". An integer in the range (" + to_string(limits->first) + ", " +
          to_string(limits->second) + "), inclusive, is required.";
    }
    return ConcordConfiguration::ParameterStatus::INVALID;
  }
  if ((intVal < limits->first) || (intVal > limits->second)) {
    if (failureMessage) {
      *failureMessage =
          "Invalid value for parameter " + path.toString() + ": \"" + value +
          "\". An integer in the range (" + to_string(limits->first) + ", " +
          to_string(limits->second) + "), inclusive, is required.";
    }
    return ConcordConfiguration::ParameterStatus::INVALID;
  }
  return ConcordConfiguration::ParameterStatus::VALID;
}

static ConcordConfiguration::ParameterStatus validateClientProxiesPerReplica(
    const string& value, const ConcordConfiguration& config,
    const ConfigurationPath& path, string* failureMessage, void* state) {
  ConcordConfiguration::ParameterStatus res = validateUInt(
      value, config, path, failureMessage,
      const_cast<void*>(reinterpret_cast<const void*>(&kPositiveUInt16Limits)));
  if (res != ConcordConfiguration::ParameterStatus::VALID) {
    return res;
  } else {
    if (!config.hasValue<uint16_t>("f_val") ||
        !config.hasValue<uint16_t>("c_val")) {
      if (failureMessage) {
        *failureMessage =
            "Cannot fully validate client_proxies_per_replica: value is in "
            "range, but f_val and c_val must both also be known to veriy that "
            "the total number of SBFT principals needed is in range.";
      }
      return ConcordConfiguration::ParameterStatus::INSUFFICIENT_INFORMATION;
    }
    uint16_t clientProxiesPerReplica = (uint16_t)(std::stoull(value));
    return validateNumberOfPrincipalsInBounds(
        config.getValue<uint16_t>("f_val"), config.getValue<uint16_t>("c_val"),
        clientProxiesPerReplica, failureMessage);
  }
}

static ConcordConfiguration::ParameterStatus validateCryptosys(
    const string& value, const ConcordConfiguration& config,
    const ConfigurationPath& path, string* failureMessage, void* state) {
  std::pair<string, string> cryptoSelection = parseCryptosystemSelection(value);
  if (!Cryptosystem::isValidCryptosystemSelection(cryptoSelection.first,
                                                  cryptoSelection.second)) {
    if (failureMessage) {
      *failureMessage =
          "Invalid cryptosystem selection for " + path.toString() + ": \"" +
          value +
          "\" is not a recognized and supported selection of cryptosystem.";
    }
    return ConcordConfiguration::ParameterStatus::INVALID;
  }

  if (!config.hasValue<uint16_t>("f_val") ||
      !config.hasValue<uint16_t>("c_val")) {
    if (failureMessage) {
      *failureMessage = "Cannot fully validate cryptosystem selection for " +
                        path.toString() +
                        ": f_val and c_val must be known to determine the "
                        "threshold and total number of signers.";
    }
    return ConcordConfiguration::ParameterStatus::INSUFFICIENT_INFORMATION;
  }

  uint16_t fVal = config.getValue<uint16_t>("f_val");
  uint16_t cVal = config.getValue<uint16_t>("c_val");
  uint64_t unvalidatedNumSigners =
      3 * ((uint64_t)fVal) + 2 * ((uint64_t)cVal) + 1;
  if (unvalidatedNumSigners > UINT16_MAX) {
    if (failureMessage) {
      *failureMessage =
          "Cryptosystem selection for " + path.toString() +
          " cannot be valid: f_val(" + to_string(fVal) + ") and c_val(" +
          to_string(cVal) + ") imply the number of SBFT replicas(" +
          to_string(unvalidatedNumSigners) +
          "), and by extension the number of threshold signers for this "
          "cryptosystem, exceeds the currently supported limit of " +
          to_string(UINT16_MAX) + ".";
    }
    return ConcordConfiguration::ParameterStatus::INVALID;
  }

  uint16_t numSigners = (uint16_t)unvalidatedNumSigners;
  uint16_t threshold;

  // Compute the threshold, which is different for each of the three
  // cryptosystems we are currently using. The code specific to each
  // cryptosystem here will hopefully be factored out into its own function in a
  // futre round of code cleanup in which we intend to replace
  // validator/generator/scopesizer function pointers with objects.
  string sysName = path.getLeaf().name;
  if (sysName == "slow_commit_cryptosys") {
    threshold = 2 * fVal + cVal + 1;
  } else if (sysName == "commit_cryptosys") {
    threshold = 3 * fVal + cVal + 1;
  } else if (sysName == "optimistic_commit_cryptosys") {
    threshold = 3 * fVal + 2 * cVal + 1;
  } else {
    if (failureMessage) {
      *failureMessage =
          "Cannot determine validity of cryptosystem selection for " +
          path.toString() +
          ": The validator lacks knowledge of the correct threshold for this "
          "cryptosystem.";
    }
    return ConcordConfiguration::ParameterStatus::INSUFFICIENT_INFORMATION;
  }

  if (!Cryptosystem::isValidCryptosystemSelection(cryptoSelection.first,
                                                  cryptoSelection.second,
                                                  numSigners, threshold)) {
    if (failureMessage) {
      *failureMessage = "Invalid cryptosystem selection for " +
                        path.toString() + ": cryptosytem selection \"" + value +
                        "\" is not supported with the required threshold(" +
                        to_string(threshold) + ") and number of signers(" +
                        to_string(numSigners) + ") for this system.";
    }
    return ConcordConfiguration::ParameterStatus::INVALID;
  }
  return ConcordConfiguration::ParameterStatus::VALID;
}

static ConcordConfiguration::ParameterStatus validatePublicKey(
    const string& value, const ConcordConfiguration& config,
    const ConfigurationPath& path, string* failureMessage, void* state) {
  assert(state);
  std::unique_ptr<Cryptosystem>* cryptosystemPointer =
      static_cast<std::unique_ptr<Cryptosystem>*>(state);

  if (!(*cryptosystemPointer)) {
    if (failureMessage) {
      *failureMessage = "Cannot assess validity of threshold public key " +
                        path.toString() +
                        ": corresponding cryptosystem is not initialized.";
    }
    return ConcordConfiguration::ParameterStatus::INSUFFICIENT_INFORMATION;
  }
  if (!((*cryptosystemPointer)->isValidPublicKey(value))) {
    if (failureMessage) {
      *failureMessage = "Invalid threshold public key for " + path.toString() +
                        ": \"" + value + "\".";
    }
    return ConcordConfiguration::ParameterStatus::INVALID;
  }
  return ConcordConfiguration::ParameterStatus::VALID;
}

static ConcordConfiguration::ParameterStatus getThresholdPublicKey(
    const ConcordConfiguration& config, const ConfigurationPath& path,
    string* output, void* state) {
  assert(state);
  std::unique_ptr<Cryptosystem>* cryptosystemPointer =
      static_cast<std::unique_ptr<Cryptosystem>*>(state);

  if (!(*cryptosystemPointer)) {
    return ConcordConfiguration::ParameterStatus::INSUFFICIENT_INFORMATION;
  }
  try {
    *output = (*cryptosystemPointer)->getSystemPublicKey();
    return ConcordConfiguration::ParameterStatus::VALID;
  } catch (UninitializedCryptosystemException& e) {
    return ConcordConfiguration::ParameterStatus::INSUFFICIENT_INFORMATION;
  }
}

static ConcordConfiguration::ParameterStatus validateCVal(
    const string& value, const ConcordConfiguration& config,
    const ConfigurationPath& path, string* failureMessage, void* state) {
  ConcordConfiguration::ParameterStatus res = validateUInt(
      value, config, path, failureMessage,
      const_cast<void*>(reinterpret_cast<const void*>(&kUInt16Limits)));
  if (res != ConcordConfiguration::ParameterStatus::VALID) {
    return res;
  } else {
    if (!config.hasValue<uint16_t>("f_val") ||
        !config.hasValue<uint16_t>("client_proxies_per_replica")) {
      if (failureMessage) {
        *failureMessage =
            "Cannot fully validate c_val: value is in range, but f_val and "
            "client_proxies_per_replica must both also be known to veriy that "
            "the total number of SBFT principals needed is in range.";
      }
      return ConcordConfiguration::ParameterStatus::INSUFFICIENT_INFORMATION;
    }
    uint16_t cVal = (uint16_t)(std::stoull(value));
    return validateNumberOfPrincipalsInBounds(
        config.getValue<uint16_t>("f_val"), cVal,
        config.getValue<uint16_t>("client_proxies_per_replica"),
        failureMessage);
  }
}

static ConcordConfiguration::ParameterStatus validateFVal(
    const string& value, const ConcordConfiguration& config,
    const ConfigurationPath& path, string* failureMessage, void* state) {
  ConcordConfiguration::ParameterStatus res = validateUInt(
      value, config, path, failureMessage,
#ifdef ALLOW_0_FAULT_TOLERANCE
      const_cast<void*>(reinterpret_cast<const void*>(&kUInt16Limits)));
#else
      const_cast<void*>(reinterpret_cast<const void*>(&kPositiveUInt16Limits)));
#endif
  if (res != ConcordConfiguration::ParameterStatus::VALID) {
    return res;
  }
  if (!config.hasValue<uint16_t>("f_val") ||
      !config.hasValue<uint16_t>("client_proxies_per_replica")) {
    if (failureMessage) {
      *failureMessage =
          "Cannot fully validate f_val: value is in range, but c_val and "
          "client_proxies_per_replica must both also be known to veriy that "
          "the total number of SBFT principals needed is in range.";
    }
    return ConcordConfiguration::ParameterStatus::INSUFFICIENT_INFORMATION;
  }
  uint16_t fVal = (uint16_t)(std::stoull(value));
  return validateNumberOfPrincipalsInBounds(
      fVal, config.getValue<uint16_t>("c_val"),
      config.getValue<uint16_t>("client_proxies_per_replica"), failureMessage);
}

static ConcordConfiguration::ParameterStatus validateNumClientProxies(
    const string& value, const ConcordConfiguration& config,
    const ConfigurationPath& path, string* failureMessage, void* state) {
  ConcordConfiguration::ParameterStatus res = validateUInt(
      value, config, path, failureMessage,
      const_cast<void*>(reinterpret_cast<const void*>(&kPositiveUInt16Limits)));
  if (res != ConcordConfiguration::ParameterStatus::VALID) {
    return res;
  }
  if (!config.hasValue<uint16_t>("f_val") ||
      !config.hasValue<uint16_t>("c_val") ||
      !config.hasValue<uint16_t>("client_proxies_per_replica")) {
    if (failureMessage) {
      *failureMessage =
          "Cannot validate num_client_proxies: values for f_val, c_val and "
          "client_proxies_per_replica are required to determine expected value "
          "of num_client_proxies.";
    }
    return ConcordConfiguration::ParameterStatus::INSUFFICIENT_INFORMATION;
  }
  uint16_t expectedNumClientProxies =
      config.getValue<uint16_t>("client_proxies_per_replica") *
      (3 * config.getValue<uint16_t>("f_val") +
       2 * config.getValue<uint16_t>("c_val") + 1);
  if ((uint16_t)(std::stoull(value)) != expectedNumClientProxies) {
    if (failureMessage) {
      *failureMessage =
          "Invalid valud for num_client_proxies: " + value +
          "; num_client_proxies must be equal to client_proxies_per_replica * "
          "(3 * f_val + 2 * c_val + 1).";
    }
    return ConcordConfiguration::ParameterStatus::INVALID;
  }
  return ConcordConfiguration::ParameterStatus::VALID;
}

static ConcordConfiguration::ParameterStatus computeNumClientProxies(
    const ConcordConfiguration& config, const ConfigurationPath& path,
    string* output, void* state) {
  if (!config.hasValue<uint16_t>("f_val") ||
      !config.hasValue<uint16_t>("c_val") ||
      !config.hasValue<uint16_t>("client_proxies_per_replica")) {
    return ConcordConfiguration::ParameterStatus::INSUFFICIENT_INFORMATION;
  }
  *output = to_string(config.getValue<uint16_t>("client_proxies_per_replica") *
                      (3 * config.getValue<uint16_t>("f_val") +
                       2 * config.getValue<uint16_t>("c_val") + 1));
  return ConcordConfiguration::ParameterStatus::VALID;
}

static ConcordConfiguration::ParameterStatus validateNumPrincipals(
    const string& value, const ConcordConfiguration& config,
    const ConfigurationPath& path, string* failureMessage, void* state) {
  ConcordConfiguration::ParameterStatus res = validateUInt(
      value, config, path, failureMessage,
      const_cast<void*>(reinterpret_cast<const void*>(&kPositiveUInt16Limits)));
  if (res != ConcordConfiguration::ParameterStatus::VALID) {
    return res;
  }
  if (!config.hasValue<uint16_t>("f_val") ||
      !config.hasValue<uint16_t>("c_val") ||
      !config.hasValue<uint16_t>("client_proxies_per_replica")) {
    if (failureMessage) {
      *failureMessage =
          "Cannot validate num_principals: values for f_val, c_val and "
          "client_proxies_per_replica are required to determine expected value "
          "of num_principals.";
    }
    return ConcordConfiguration::ParameterStatus::INSUFFICIENT_INFORMATION;
  }
  uint16_t expectedNumPrincipals =
      (config.getValue<uint16_t>("client_proxies_per_replica") + 1) *
      (3 * config.getValue<uint16_t>("f_val") +
       2 * config.getValue<uint16_t>("c_val") + 1);
  if ((uint16_t)(std::stoull(value)) != expectedNumPrincipals) {
    if (failureMessage) {
      *failureMessage =
          "Invalid value for num_principals: " + value +
          "; num_principals must be equal to (1 + client_proxies_per_replica) "
          "* (3 * f_val + 2 * c_val + 1).";
    }
    return ConcordConfiguration::ParameterStatus::INVALID;
  }
  return ConcordConfiguration::ParameterStatus::VALID;
}

static ConcordConfiguration::ParameterStatus computeNumPrincipals(
    const ConcordConfiguration& config, const ConfigurationPath& path,
    string* output, void* state) {
  if (!config.hasValue<uint16_t>("f_val") ||
      !config.hasValue<uint16_t>("c_val") ||
      !config.hasValue<uint16_t>("client_proxies_per_replica")) {
    return ConcordConfiguration::ParameterStatus::INSUFFICIENT_INFORMATION;
  }
  *output =
      to_string((1 + config.getValue<uint16_t>("client_proxies_per_replica")) *
                (3 * config.getValue<uint16_t>("f_val") +
                 2 * config.getValue<uint16_t>("c_val") + 1));
  return ConcordConfiguration::ParameterStatus::VALID;
}

static ConcordConfiguration::ParameterStatus validateNumReplicas(
    const string& value, const ConcordConfiguration& config,
    const ConfigurationPath& path, string* failureMessage, void* state) {
  ConcordConfiguration::ParameterStatus res = validateUInt(
      value, config, path, failureMessage,
      const_cast<void*>(reinterpret_cast<const void*>(&kPositiveUInt16Limits)));
  if (res != ConcordConfiguration::ParameterStatus::VALID) {
    return res;
  }
  if (!config.hasValue<uint16_t>("f_val") ||
      !config.hasValue<uint16_t>("c_val") ||
      !config.hasValue<uint16_t>("client_proxies_per_replica")) {
    if (failureMessage) {
      *failureMessage =
          "Cannot validate num_replicas: values for f_val, c_val and "
          "client_proxies_per_replica are required to determine expected value "
          "of num_replicas.";
    }
    return ConcordConfiguration::ParameterStatus::INSUFFICIENT_INFORMATION;
  }
  uint16_t expectedNumReplicas = 3 * config.getValue<uint16_t>("f_val") +
                                 2 * config.getValue<uint16_t>("c_val") + 1;
  if ((uint16_t)(std::stoull(value)) != expectedNumReplicas) {
    if (failureMessage) {
      *failureMessage =
          "Invalid value for num_replicas: " + value +
          "; num_replicas must be equal to 3 * f_val + 2 * c_val + 1.";
    }
    return ConcordConfiguration::ParameterStatus::INVALID;
  }
  return ConcordConfiguration::ParameterStatus::VALID;
}

static ConcordConfiguration::ParameterStatus computeNumReplicas(
    const ConcordConfiguration& config, const ConfigurationPath& path,
    string* output, void* state) {
  if (!config.hasValue<uint16_t>("f_val") ||
      !config.hasValue<uint16_t>("c_val") ||
      !config.hasValue<uint16_t>("client_proxies_per_replica")) {
    return ConcordConfiguration::ParameterStatus::INSUFFICIENT_INFORMATION;
  }
  *output = to_string(3 * config.getValue<uint16_t>("f_val") +
                      2 * config.getValue<uint16_t>("c_val") + 1);
  return ConcordConfiguration::ParameterStatus::VALID;
}

static ConcordConfiguration::ParameterStatus validatePositiveReplicaInt(
    const string& value, const ConcordConfiguration& config,
    const ConfigurationPath& path, string* failureMessage, void* state) {
  const std::pair<unsigned long long, unsigned long long>* limits;
  if (config.getConfigurationStateLabel() == "concord_node") {
    limits = &kPositiveIntLimits;
  } else {
    limits = &kPositiveULongLongLimits;
  }
  return validateUInt(value, config, path, failureMessage,
                      const_cast<void*>(reinterpret_cast<const void*>(limits)));
}

static ConcordConfiguration::ParameterStatus validateDatabaseImplementation(
    const string& value, const ConcordConfiguration& config,
    const ConfigurationPath& path, string* failureMessage, void* state) {
  if (!((value == "memory") || (value == "rocksdb"))) {
    if (failureMessage) {
      *failureMessage =
          "Unrecognized database implementation: \"" + value + "\".";
    }
    return ConcordConfiguration::ParameterStatus::INVALID;
  }
  return ConcordConfiguration::ParameterStatus::VALID;
}

static ConcordConfiguration::ParameterStatus validatePortNumber(
    const string& value, const ConcordConfiguration& config,
    const ConfigurationPath& path, string* failureMessage, void* state) {
  return validateUInt(
      value, config, path, failureMessage,
      const_cast<void*>(reinterpret_cast<const void*>(&kUInt16Limits)));
}

static ConcordConfiguration::ParameterStatus validatePrivateKey(
    const string& value, const ConcordConfiguration& config,
    const ConfigurationPath& path, string* failureMessage, void* state) {
  assert(state);
  std::unique_ptr<Cryptosystem>* cryptosystemPointer =
      static_cast<std::unique_ptr<Cryptosystem>*>(state);

  if (!(*cryptosystemPointer)) {
    if (failureMessage) {
      *failureMessage = "Cannot assess validity of threshold private key " +
                        path.toString() +
                        ": corresponding cryptosystem is not initialized.";
    }
    return ConcordConfiguration::ParameterStatus::INSUFFICIENT_INFORMATION;
  }
  if (!((*cryptosystemPointer)->isValidPrivateKey(value))) {
    if (failureMessage) {
      *failureMessage = "Invalid threshold private key for " + path.toString() +
                        ": \"" + value + "\".";
    }
    return ConcordConfiguration::ParameterStatus::INVALID;
  }
  return ConcordConfiguration::ParameterStatus::VALID;
}

static ConcordConfiguration::ParameterStatus getThresholdPrivateKey(
    const ConcordConfiguration& config, const ConfigurationPath& path,
    string* output, void* state) {
  assert(state);
  std::unique_ptr<Cryptosystem>* cryptosystemPointer =
      static_cast<std::unique_ptr<Cryptosystem>*>(state);

  if (!(*cryptosystemPointer)) {
    return ConcordConfiguration::ParameterStatus::INSUFFICIENT_INFORMATION;
  }
  try {
    // This validator should only be called with paths of the form:
    //
    // node[i]/replica[0]/..._private_key
    //
    // We infer which replica the key is for from this path. Note the
    // Cryptosystem class considers signers 1-indexed.
    if ((path.name != "node") || !path.isScope || !path.useInstance ||
        (path.index >= UINT16_MAX)) {
      return ConcordConfiguration::ParameterStatus::INVALID;
    }
    uint16_t signer = path.index + 1;

    *output = (*cryptosystemPointer)->getPrivateKey(signer);
    return ConcordConfiguration::ParameterStatus::VALID;
  } catch (UninitializedCryptosystemException& e) {
    return ConcordConfiguration::ParameterStatus::INSUFFICIENT_INFORMATION;
  }
}

static ConcordConfiguration::ParameterStatus validateVerificationKey(
    const string& value, const ConcordConfiguration& config,
    const ConfigurationPath& path, string* failureMessage, void* state) {
  assert(state);
  std::unique_ptr<Cryptosystem>* cryptosystemPointer =
      static_cast<std::unique_ptr<Cryptosystem>*>(state);

  if (!(*cryptosystemPointer)) {
    if (failureMessage) {
      *failureMessage =
          "Cannot assess validity of threshold verification key " +
          path.toString() + ": corresponding cryptosystem is not initialized.";
    }
    return ConcordConfiguration::ParameterStatus::INSUFFICIENT_INFORMATION;
  }
  if (!((*cryptosystemPointer)->isValidVerificationKey(value))) {
    if (failureMessage) {
      *failureMessage = "Invalid threshold verification key for " +
                        path.toString() + ": \"" + value + "\".";
    }
    return ConcordConfiguration::ParameterStatus::INVALID;
  }
  return ConcordConfiguration::ParameterStatus::VALID;
}

static ConcordConfiguration::ParameterStatus getThresholdVerificationKey(
    const ConcordConfiguration& config, const ConfigurationPath& path,
    string* output, void* state) {
  assert(state);
  std::unique_ptr<Cryptosystem>* cryptosystemPointer =
      static_cast<std::unique_ptr<Cryptosystem>*>(state);

  if (!(*cryptosystemPointer)) {
    return ConcordConfiguration::ParameterStatus::INSUFFICIENT_INFORMATION;
  }
  try {
    // This validator should only be called with paths of the form:
    //
    // node[i]/replica[0]/..._verification_key
    //
    // We infer which replica the key is for from this path. Note the
    // Cryptosystem class considers signers 1-indexed.
    if ((path.name != "node") || !path.isScope || !path.useInstance ||
        (path.index >= UINT16_MAX)) {
      return ConcordConfiguration::ParameterStatus::INVALID;
    }
    uint16_t signer = path.index + 1;

    *output = ((*cryptosystemPointer)->getSystemVerificationKeys())[signer];
    return ConcordConfiguration::ParameterStatus::VALID;
  } catch (UninitializedCryptosystemException& e) {
    return ConcordConfiguration::ParameterStatus::INSUFFICIENT_INFORMATION;
  }
}

static ConcordConfiguration::ParameterStatus validatePrincipalId(
    const string& value, const ConcordConfiguration& config,
    const ConfigurationPath& path, string* failureMessage, void* state) {
  ConcordConfiguration::ParameterStatus res = validateUInt(
      value, config, path, failureMessage,
      const_cast<void*>(reinterpret_cast<const void*>(&kUInt16Limits)));
  if (res != ConcordConfiguration::ParameterStatus::VALID) {
    return res;
  }
  uint16_t principalID = (uint16_t)(std::stoull(value));

  if (!config.hasValue<uint16_t>("f_val") ||
      !config.hasValue<uint16_t>("c_val") ||
      !config.hasValue<uint16_t>("client_proxies_per_replica")) {
    if (failureMessage) {
      *failureMessage =
          "Cannot fully validate Concord-BFT principal ID for " +
          path.toString() +
          ": f_val, c_val, and client_proxies_per_replica are required to "
          "determine bounds for maximum principal ID.";
    }
    return ConcordConfiguration::ParameterStatus::INSUFFICIENT_INFORMATION;
  }
  uint16_t fVal = config.getValue<uint16_t>("f_val");
  uint16_t cVal = config.getValue<uint16_t>("c_val");
  uint16_t numReplicas = 3 * fVal + 2 * cVal + 1;
  uint16_t clientProxiesPerReplica =
      config.getValue<uint16_t>("client_proxies_per_replica");
  uint16_t numPrincipals = numReplicas * (1 + clientProxiesPerReplica);

  // The path to a principal Id should be of one of these forms:
  //   node[i]/replica[0]/principal_id
  //   node[i]/client_proxy[j]/principal_id
  assert(path.isScope && path.subpath);

  if (path.subpath->name == "replica") {
    if (principalID >= numReplicas) {
      if (failureMessage) {
        *failureMessage =
            "Invalid principal ID for " + path.toString() + ": " +
            to_string(principalID) +
            ". Principal IDs for replicas must be less than num_replicas.";
      }
      return ConcordConfiguration::ParameterStatus::INVALID;
    }

  } else {
    assert(path.subpath->name == "client_proxy");

    if ((principalID < numReplicas) || (principalID >= numPrincipals)) {
      if (failureMessage) {
        *failureMessage =
            "Invalid principal ID for " + path.toString() + ": " +
            to_string(principalID) +
            ". Principal IDs for client proxies should be in the range "
            "(num_replicas, num_principals - 1), inclusive.";
      }
      return ConcordConfiguration::ParameterStatus::INVALID;
    }
  }

  res = ConcordConfiguration::ParameterStatus::VALID;
  for (size_t i = 0; i < numReplicas; ++i) {
    ConfigurationPath replicaPath("node", (size_t)i);
    replicaPath.subpath.reset(new ConfigurationPath("replica", (size_t)0));
    replicaPath.subpath->subpath.reset(
        new ConfigurationPath("principal_id", false));
    if (!config.hasValue<uint16_t>(replicaPath)) {
      if (replicaPath != path) {
        res = ConcordConfiguration::ParameterStatus::INSUFFICIENT_INFORMATION;
      }
    } else if ((config.getValue<uint16_t>(replicaPath) == principalID) &&
               (replicaPath != path)) {
      if (failureMessage) {
        *failureMessage = "Invalid principal ID for " + path.toString() + ": " +
                          to_string(principalID) +
                          ". This ID is non-unique; it duplicates the ID for " +
                          replicaPath.toString() + ".";
      }
      return ConcordConfiguration::ParameterStatus::INVALID;
    }

    for (size_t j = 0; j < clientProxiesPerReplica; ++j) {
      ConfigurationPath clientProxyPath("node", (size_t)i);
      clientProxyPath.subpath.reset(new ConfigurationPath("client_proxy", j));
      clientProxyPath.subpath->subpath.reset(
          new ConfigurationPath("principal_id", false));
      if (!config.hasValue<uint16_t>(clientProxyPath)) {
        if (clientProxyPath != path) {
          res = ConcordConfiguration::ParameterStatus::INSUFFICIENT_INFORMATION;
        }
      } else if ((config.getValue<uint16_t>(clientProxyPath) == principalID) &&
                 (clientProxyPath != path)) {
        if (failureMessage) {
          *failureMessage =
              "Invalid principal ID for " + path.toString() + ": " +
              to_string(principalID) +
              ". This ID is non-unique; it duplicates the ID for " +
              clientProxyPath.toString() + ".";
        }
        return ConcordConfiguration::ParameterStatus::INVALID;
      }
    }
  }

  if (failureMessage &&
      (res ==
       ConcordConfiguration::ParameterStatus::INSUFFICIENT_INFORMATION)) {
    *failureMessage = "Cannot fully validate principal ID for " +
                      path.toString() +
                      ": Not all other principal IDs are known, but are "
                      "required to check for uniqueness.";
  }
  return ConcordConfiguration::ParameterStatus::VALID;
}

static ConcordConfiguration::ParameterStatus computePrincipalId(
    const ConcordConfiguration& config, const ConfigurationPath& path,
    string* output, void* state) {
  // The path to a principal Id should be of one of these forms:
  //   node[i]/replica[0]/principal_id
  //   node[i]/client_proxy[j]/principal_id

  assert(path.isScope && path.subpath && path.useInstance);

  if (path.subpath->name == "replica") {
    *output = to_string(path.index);
  } else {
    assert((path.subpath->name == "client_proxy") && path.subpath->isScope &&
           path.subpath->useInstance);

    if (!config.hasValue<uint16_t>("f_val") ||
        !config.hasValue<uint16_t>("c_val")) {
      return ConcordConfiguration::ParameterStatus::INSUFFICIENT_INFORMATION;
    }
    uint16_t numReplicas = 3 * config.getValue<uint16_t>("f_val") +
                           2 * config.getValue<uint16_t>("c_val") + 1;

    *output = to_string(path.index + numReplicas * (1 + path.subpath->index));
  }

  return ConcordConfiguration::ParameterStatus::VALID;
}

static ConcordConfiguration::ParameterStatus computeTimeSourceId(
    const ConcordConfiguration& config, const ConfigurationPath& path,
    string* output, void* state) {
  *output = "time-source" + to_string(path.index);
  return ConcordConfiguration::ParameterStatus::VALID;
}

const size_t kRSAPublicKeyHexadecimalLength = 584;
// Note we do not have a correpsonding kRSAPrivateKeyHexadecimalLength constant
// because the hexadecimal length of RSA private keys actually seems to vary a
// little in the current serialization of them.

static ConcordConfiguration::ParameterStatus validateRSAPrivateKey(
    const string& value, const ConcordConfiguration& config,
    const ConfigurationPath& path, string* failureMessage, void* state) {
  if (!(std::regex_match(value, std::regex("[0-9A-Fa-f]+")))) {
    return ConcordConfiguration::ParameterStatus::INVALID;
  }
  return ConcordConfiguration::ParameterStatus::VALID;
}

static ConcordConfiguration::ParameterStatus getRSAPrivateKey(
    const ConcordConfiguration& config, const ConfigurationPath& path,
    string* output, void* state) {
  // The path to an RSA private key should be of the form:
  //   node[i]/replica[0]/private_key
  // We infer what replica the key is for from the path.
  assert(path.isScope && path.useInstance && path.subpath &&
         (path.subpath->name == "replica"));
  const ConcordPrimaryConfigurationAuxiliaryState* auxState =
      dynamic_cast<const ConcordPrimaryConfigurationAuxiliaryState*>(
          config.getAuxiliaryState());
  assert(auxState);

  size_t nodeIndex = path.index;

  if (nodeIndex >= auxState->replicaRSAKeys.size()) {
    return ConcordConfiguration::ParameterStatus::INSUFFICIENT_INFORMATION;
  }
  *output = auxState->replicaRSAKeys[nodeIndex].first;
  return ConcordConfiguration::ParameterStatus::VALID;
}

static ConcordConfiguration::ParameterStatus validateRSAPublicKey(
    const string& value, const ConcordConfiguration& config,
    const ConfigurationPath& path, string* failureMessage, void* state) {
  if (!((value.length() == kRSAPublicKeyHexadecimalLength) &&
        std::regex_match(value, std::regex("[0-9A-Fa-f]+")))) {
    return ConcordConfiguration::ParameterStatus::INVALID;
  }
  return ConcordConfiguration::ParameterStatus::VALID;
}

static ConcordConfiguration::ParameterStatus getRSAPublicKey(
    const ConcordConfiguration& config, const ConfigurationPath& path,
    string* output, void* state) {
  // The path to an RSA public key should be of this form:
  //   node[i]/replica[0]/public_key
  // We infer what replica the key is for from the path.
  assert(path.isScope && path.useInstance && path.subpath &&
         (path.subpath->name == "replica"));
  const ConcordPrimaryConfigurationAuxiliaryState* auxState =
      dynamic_cast<const ConcordPrimaryConfigurationAuxiliaryState*>(
          config.getAuxiliaryState());
  assert(auxState);

  size_t nodeIndex = path.index;

  if (nodeIndex >= auxState->replicaRSAKeys.size()) {
    return ConcordConfiguration::ParameterStatus::INSUFFICIENT_INFORMATION;
  }
  *output = auxState->replicaRSAKeys[nodeIndex].second;
  return ConcordConfiguration::ParameterStatus::VALID;
}

// We generally do not validate hosts as we want to allow use of either
// hostnames or IP addresses in the configuration files in various contexts
// without the configuration generation utility concerning itself about this;
// however, one constraint we do want to validate is that, when a node reads its
// configuration file, node-local hosts are set to loopback if the
// "use_loopback_for_local_hosts" option is enabled.
static ConcordConfiguration::ParameterStatus validatePrincipalHost(
    const string& value, const ConcordConfiguration& config,
    const ConfigurationPath& path, string* failureMessage, void* state) {
  // Enforcing that loopback is expected for node-local hosts does not make
  // sense unless we have a specific node's configuration and this principal
  // host belongs to a particular node.
  if ((config.getConfigurationStateLabel() != "concord_node") ||
      !path.isScope || !path.useInstance || (path.name != "node")) {
    return ConcordConfiguration::ParameterStatus::VALID;
  }

  // We make a copy of the configuration to use for determining which node is
  // local because this process may require iteration of the configuration, and,
  // under the implementations at the time of this writing, a const
  // ConcordConfiguration cannot be iterated.
  ConcordConfiguration nonConstConfig = config;

  if (config.hasValue<bool>("use_loopback_for_local_hosts") &&
      config.getValue<bool>("use_loopback_for_local_hosts") &&
      (value != "127.0.0.1")) {
    // Note that, depending on the order order in which parameters are loaded,
    // we may not yet be able to tell which node is local in this configuration
    // at the time this validator is called when first loading hosts into the
    // config. To handle this case, we return INSUFFICIENT_INFORMATION if
    // detectLocalNode throws an exception.
    size_t local_node_index;
    try {
      local_node_index = detectLocalNode(nonConstConfig);
    } catch (const ConfigurationResourceNotFoundException& e) {
      return ConcordConfiguration::ParameterStatus::INSUFFICIENT_INFORMATION;
    }
    if (path.index == local_node_index) {
      *failureMessage = "Invalid host address for " + path.toString() + ": " +
                        value +
                        "; the value 127.0.0.1 (i.e. loopback) is expected for "
                        "this host since it is local to this node and "
                        "use_loopback_for_local_hosts is enabled.";
      return ConcordConfiguration::ParameterStatus::INVALID;
    }
  }
  return ConcordConfiguration::ParameterStatus::VALID;
}

const unordered_set<string> timeVerificationOptions({"rsa-time-signing",
                                                     "bft-client-proxy-id",
                                                     "none"});

static ConcordConfiguration::ParameterStatus validateEnumeratedOption(
    const string& value, const ConcordConfiguration& config,
    const ConfigurationPath& path, string* failureMessage, void* state) {
  assert(state);
  const unordered_set<string>* options =
      const_cast<const unordered_set<string>*>(
          static_cast<unordered_set<string>*>(state));
  if (options->count(value) < 1) {
    *failureMessage = "Unrecognized or unsupported value for " +
                      path.toString() + ": \"" + value +
                      "\". Recognized values include: ";
    bool was_first = true;
    for (const auto& option : *options) {
      if (!was_first) {
        *failureMessage += ", ";
      }
      *failureMessage += "\"" + option + "\"";
      was_first = false;
    }
    *failureMessage += ".";
    return ConcordConfiguration::ParameterStatus::INVALID;
  }
  return ConcordConfiguration::ParameterStatus::VALID;
}

// Implementation of specifyConfiguration and other utility functions that
// encode knowledge about the current configuration.

// This function is intended to serve as a single source of truth for all places
// that need to know the current Concord configuration format. If you need to
// add new configuration parameters or otherwise change the format of our
// configuration files, please add to or modify the code in this function.
void specifyConfiguration(ConcordConfiguration& config) {
  config.clear();

  // Auxiliary State initialization
  ConcordPrimaryConfigurationAuxiliaryState* auxState =
      new ConcordPrimaryConfigurationAuxiliaryState();
  config.setAuxiliaryState(auxState);

  // Scope declarations
  config.declareScope(
      "node",
      "Concord nodes, the nodes that form the distributed system that "
      "maintains a blockchain in Concord. Each node runs in its own process, "
      "and, in a production deployment, each node should be on a different "
      "machine. Ideally, the nodes should also be split up into different "
      "fault domains.",
      sizeNodes, nullptr);
  ConcordConfiguration& node = config.subscope("node");

  node.declareScope(
      "replica",
      "SBFT replicas, which serve as the core replicas for Byzantine fault "
      "tolerant consensus in a Concord deployment. At the time of this "
      "writing, there generally should be no more than one SBFT replica per "
      "Concord node, and ideally the SBFT replicas should be split up into "
      "separate fault domains.",
      sizeReplicas, nullptr);
  ConcordConfiguration& replica = node.subscope("replica");

  node.declareScope(
      "client_proxy",
      "SBFT client proxies; these client proxies serve to connect the SBFT "
      "replicas to whatever may be accessing and using the blockchain that the "
      "replicas maintain. The client proxies communicate with the SBFT "
      "replicas to forward requests incoming to the Concord system and to "
      "fetch information from the blockchain to be served externally, but the "
      "client proxies are not \"voting\" participants in establishing "
      "consensus.",
      sizeClientProxies, nullptr);
  ConcordConfiguration& clientProxy = node.subscope("client_proxy");

  vector<string> privateGeneratedTags(
      {"config_generation_time", "generated", "private"});
  vector<string> publicGeneratedTags(
      {"config_generation_time", "generated", "public"});
  vector<string> publicInputTags({"config_generation_time", "input", "public"});
  vector<string> principalHostTags(
      {"config_generation_time", "could_be_loopback", "input", "public"});
  vector<string> defaultableByUtilityTags(
      {"config_generation_time", "defaultable", "public"});
  vector<string> privateInputTags({"input", "private"});
  vector<string> defaultableByReplicaTags({"defaultable", "private"});
  vector<string> privateOptionalTags({"optional", "private"});
  vector<string> publicOptionalTags({"optional", "public"});
  vector<string> publicDefaultableTags({"defaultable", "public"});

  // Global optional config values
  vector<string> optionalTags({"optional"});

  // Parameter declarations
  config.declareParameter("client_proxies_per_replica",
                          "The number of SBFT client proxies to create on each "
                          "Concord node with each SBFT replica.");
  config.tagParameter("client_proxies_per_replica", publicInputTags);
  config.addValidator("client_proxies_per_replica",
                      validateClientProxiesPerReplica, nullptr);

  config.declareParameter(
      "commit_cryptosys",
      "Type of cryptosystem to use to commit transactions in the SBFT general "
      "path (threshold 3F + C + 1). This parameter should consist of two "
      "space-separated strings, the first of which names the cryptosystem type "
      "and the second of which is a type-specific parameter or subtype "
      "selection (for example, the second string might be an eliptic curve "
      "type if an elliptic curve cryptosystem is selected).",
      "threshold-bls BN-P254");
  config.tagParameter("commit_cryptosys", defaultableByUtilityTags);
  config.addValidator("commit_cryptosys", validateCryptosys, nullptr);

  config.declareParameter(
      "commit_public_key",
      "Public key for the general path commit cryptosystem.");
  config.tagParameter("commit_public_key", publicGeneratedTags);
  config.addValidator("commit_public_key", validatePublicKey,
                      &(auxState->commitCryptosys));
  config.addGenerator("commit_public_key", getThresholdPublicKey,
                      &(auxState->commitCryptosys));

  config.declareParameter(
      "concord-bft_communication_buffer_length",
      "Size of buffers to be used for messages exchanged with and within "
      "Concord-BFT. Note that the capacity of these buffers may limit things "
      "like the maximum sizes of transactions or replies to requests that can "
      "be handled.",
      "64000");
  config.tagParameter("concord-bft_communication_buffer_length",
                      defaultableByUtilityTags);
  config.addValidator("concord-bft_communication_buffer_length", validateUInt,
                      const_cast<void*>(reinterpret_cast<const void*>(
                          &kConcordBFTCommunicationBufferSizeLimits)));

  // TODO: The following parameters should be completely optional because
  // its default values are within concord-bft
  config.declareParameter("concord-bft_max_external_message_size",
                          "Maximum external message size");
  config.tagParameter("concord-bft_max_external_message_size", optionalTags);
  config.addValidator(
      "concord-bft_max_external_message_size", validateUInt,
      const_cast<void*>(reinterpret_cast<const void*>(&kUInt32Limits)));

  config.declareParameter("concord-bft_max_reply_message_size",
                          "Maximum reply message size");
  config.tagParameter("concord-bft_max_reply_message_size", optionalTags);
  config.addValidator(
      "concord-bft_max_reply_message_size", validateUInt,
      const_cast<void*>(reinterpret_cast<const void*>(&kUInt32Limits)));

  config.declareParameter("concord-bft_max_num_of_reserved_pages",
                          "Maximum number of reserved pages");
  config.tagParameter("concord-bft_max_num_of_reserved_pages", optionalTags);
  config.addValidator(
      "concord-bft_max_num_of_reserved_pages", validateUInt,
      const_cast<void*>(reinterpret_cast<const void*>(&kUInt32Limits)));

  config.declareParameter("concord-bft_size_of_reserved_page",
                          "Size of a reserved page");
  config.tagParameter("concord-bft_size_of_reserved_page", optionalTags);
  config.addValidator(
      "concord-bft_size_of_reserved_page", validateUInt,
      const_cast<void*>(reinterpret_cast<const void*>(&kUInt32Limits)));

  config.declareParameter("concurrency_level",
                          "Number of consensus operations that Concord-BFT may "
                          "execute in parallel.",
                          "3");
  config.tagParameter("concurrency_level", defaultableByUtilityTags);
  config.addValidator(
      "concurrency_level", validateUInt,
      const_cast<void*>(reinterpret_cast<const void*>(&kPositiveUInt16Limits)));

  config.declareParameter(
      "c_val",
      "C parameter to the SBFT algorithm, that is, the number of slow, "
      "crashed, or otherwise non-responsive replicas that can be tolerated "
      "before having to fall back on a slow path for consensus.");
  config.tagParameter("c_val", publicInputTags);
  config.addValidator("c_val", validateCVal, nullptr);

  config.declareParameter(
      "f_val",
      "F parameter to the SBFT algorithm, that is, the number of "
      "Byzantine-faulty replicas that can be tolerated in the system before "
      "safety guarantees are lost.");
  config.tagParameter("f_val", publicInputTags);
  config.addValidator("f_val", validateFVal, nullptr);

  config.declareParameter(
      "gas_limit",
      "Ethereum gas limit to enforce on all transactions; this prevents "
      "transactions that either fail to terminate or take excessively long to "
      "do so from burdening the system.",
      "10000000");
  config.tagParameter("gas_limit", defaultableByUtilityTags);
  config.addValidator(
      "gas_limit", validateUInt,
      const_cast<void*>(reinterpret_cast<const void*>(&kPositiveUInt64Limits)));

  config.declareParameter(
      "num_client_proxies",
      "Total number of Concord-BFT client proxies in this deployment.");
  config.tagParameter("num_client_proxies", publicGeneratedTags);
  config.addValidator("num_client_proxies", validateNumClientProxies, nullptr);
  config.addGenerator("num_client_proxies", computeNumClientProxies, nullptr);

  config.declareParameter("num_principals",
                          "Combined total number of replicas and Concord-BFT "
                          "client proxies in this deployment.");
  config.tagParameter("num_principals", publicGeneratedTags);
  config.addValidator("num_principals", validateNumPrincipals, nullptr);
  config.addGenerator("num_principals", computeNumPrincipals, nullptr);

  config.declareParameter(
      "num_replicas", "Total number of Concord replicas in this deployment.");
  config.tagParameter("num_replicas", publicGeneratedTags);
  config.addValidator("num_replicas", validateNumReplicas, nullptr);
  config.addGenerator("num_replicas", computeNumReplicas, nullptr);

  config.declareParameter(
      "optimistic_commit_cryptosys",
      "Type of cryptosystem to use to commit transactions in the SBFT "
      "optimistic fast path (threshold 3F + 2C + 1). This parameter should "
      "consist of two space-separated strings, the first of which names the "
      "cryptosystem type and the second of which is a type-specific parameter "
      "or subtype selection (for example, the second string might be an "
      "elliptic curve type if an elliptic curve cryptosystem is selected).",
      "multisig-bls BN-P254");
  config.tagParameter("optimistic_commit_cryptosys", defaultableByUtilityTags);
  config.addValidator("optimistic_commit_cryptosys", validateCryptosys,
                      nullptr);

  config.declareParameter(
      "optimistic_commit_public_key",
      "Public key for the optimistic fast path commit cryptosystem.");
  config.tagParameter("optimistic_commit_public_key", publicGeneratedTags);
  config.addValidator("optimistic_commit_public_key", validatePublicKey,
                      &(auxState->optimisticCommitCryptosys));
  config.addGenerator("optimistic_commit_public_key", getThresholdPublicKey,
                      &(auxState->optimisticCommitCryptosys));

  config.declareParameter(
      "pruning_enabled",
      "A flag to indicate if pruning is enabled for the replcia. If set to "
      "false, LatestPrunableBlockRequest will return 0 as a latest block("
      "indicating no blocks can be pruned) and PruneRequest will return an "
      "error. If not specified, a value of false is assumed.");
  config.tagParameter("pruning_enabled", publicOptionalTags);
  config.addValidator("pruning_enabled", validateBoolean, nullptr);

  config.declareParameter(
      "pruning_num_blocks_to_keep",
      "Minimum number of blocks to always keep in storage when pruning. If not "
      "specified, a value of 0 is assumed. If pruning_duration_to_keep_minutes "
      "is specified too, the more conservative pruning range will be used (the "
      "one that prunes less blocks).");
  config.tagParameter("pruning_num_blocks_to_keep", publicOptionalTags);
  config.addValidator(
      "pruning_num_blocks_to_keep", validateUInt,
      const_cast<void*>(reinterpret_cast<const void*>(&kUInt64Limits)));

  config.declareParameter(
      "pruning_duration_to_keep_minutes",
      "Time range (in minutes) from now to the past that determines which "
      "blocks to keep and which are older than (now - "
      "pruning_duration_to_keep_minutes) and can, therefore, be pruned. If not "
      "specified, a value of 0 is assumed. If pruning_num_blocks_to_keep is "
      "specified too, the more conservative pruning range will be used (the "
      "one that prunes less blocks). This option requires the time service to "
      "be enabled.");
  config.tagParameter("pruning_duration_to_keep_minutes", publicOptionalTags);
  config.addValidator(
      "pruning_duration_to_keep_minutes", validateUInt,
      const_cast<void*>(reinterpret_cast<const void*>(&kUInt32Limits)));

  config.declareParameter(
      "slow_commit_cryptosys",
      "Type of cryptosystem to use to commit transactions in the SBFT slow "
      "path (threshold 2F + C + 1). This parameter should consist of two "
      "space-separated strings, the first of which names the cryptosystem type "
      "and the second of which is a type-specific parameter or subtype "
      "selectioin (for example, the second string might be an elliptic curve "
      "type if an elliptic curve cryptosystem is selected).",
      "threshold-bls BN-P254");
  config.tagParameter("slow_commit_cryptosys", defaultableByUtilityTags);
  config.addValidator("slow_commit_cryptosys", validateCryptosys, nullptr);

  config.declareParameter("slow_commit_public_key",
                          "Public key for the slow path commit cryptosystem.");
  config.tagParameter("slow_commit_public_key", publicGeneratedTags);
  config.addValidator("slow_commit_public_key", validatePublicKey,
                      &(auxState->slowCommitCryptosys));
  config.addGenerator("slow_commit_public_key", getThresholdPublicKey,
                      &(auxState->slowCommitCryptosys));

  config.declareParameter(
      "status_time_interval",
      "Time interval, measured in milliseconds, at which each Concord replica "
      "should send its status to the others.",
      "3000");
  config.tagParameter("status_time_interval", defaultableByUtilityTags);
  config.addValidator(
      "status_time_interval", validateUInt,
      const_cast<void*>(reinterpret_cast<const void*>(&kPositiveUInt16Limits)));

  config.declareParameter(
      "use_loopback_for_local_hosts",
      "If this parameter is set to true, Concord will expect to use the "
      "loopback IP (i.e. 127.0.0.1) for all host addresses used for internal "
      "Concord communication. Specifically, if this parameter is set to true, "
      "the configuration generation utility will replace hosts used in "
      "internal Concord communication with \"127.0.0.1\" in the configuration "
      "file belonging to the node on which that host is located; furthermore, "
      "when Concord nodes load their configuration, they will expect every "
      "host on their own node to have this IP address, and will reject their "
      "configuration otherwise. Note this parameter should not be set to true "
      "in deployments which do not guarantee that all hosts contained in a "
      "single Concord node are on the same machine such that they can reach "
      "each other via the loopback IP.",
      "false");
  config.tagParameter("use_loopback_for_local_hosts", defaultableByUtilityTags);
  config.addValidator("use_loopback_for_local_hosts", validateBoolean, nullptr);

  config.declareParameter(
      "view_change_timeout",
      "Timeout, measured in milliseconds, after which Concord-BFT will attempt "
      "an SBFT view change if not enough replicas are responding.",
      "20000");
  config.tagParameter("view_change_timeout", defaultableByUtilityTags);
  config.addValidator(
      "view_change_timeout", validateUInt,
      const_cast<void*>(reinterpret_cast<const void*>(&kPositiveUInt16Limits)));

  config.declareParameter(
      "FEATURE_time_service",
      "Enable the Time Service, and switch Ethereum to using it.", "false");
  config.tagParameter("FEATURE_time_service", publicDefaultableTags);
  config.addValidator("FEATURE_time_service", validateBoolean, nullptr);

  config.declareParameter(
      "time_verification",
      "What mechanism to use, if any, to verify received time samples "
      "allegedly from configured time sources are legitimate and not forgeries "
      "by a malicious or otherwise Byzantine-faulty party impersonating a time "
      "source. Currently supported time verification methods are: "
      "\"rsa-time-signing\", \"bft-client-proxy-id\", and \"none\". If "
      "\"rsa-time-signing\" is selected, each time source will sign its time "
      "updates with its replica's RSA private key to prove the sample's "
      "legitimacy; these signatures will be transmitted and recorded with each "
      "time sample. If \"bft-client-proxy-id\" is selected, the time contract "
      "will scrutinize the Concord-BFT client proxy ID submitting each time "
      "update and check it matches the node for the claimed time source "
      "(Concord-BFT should guarantee it is intractable to impersonate client "
      "proxies to it without that proxy's private key). If \"none\" is "
      "selected, no verification of received time samples will be used (this "
      "is NOT recommended for production deployments).",
      "rsa-time-signing");
  config.tagParameter("time_verification", publicDefaultableTags);
  config.addValidator("time_verification", validateEnumeratedOption,
                      const_cast<void*>(reinterpret_cast<const void*>(
                          &timeVerificationOptions)));

  config.declareParameter("eth_enable", "Enable Ethereum support.", "true");
  config.tagParameter("eth_enable", publicDefaultableTags);
  config.addValidator("eth_enable", validateBoolean, nullptr);

  node.declareParameter(
      "bft_client_timeout_ms",
      "How long to wait for a command execution response, in milliseconds. "
      "Zero is treated as infinity.",
      "0");
  node.tagParameter("bft_client_timeout_ms", defaultableByReplicaTags);
  node.addValidator(
      "bft_client_timeout_ms", validateUInt,
      const_cast<void*>(reinterpret_cast<const void*>(&kUInt32Limits)));

  node.declareParameter("api_worker_pool_size",
                        "Number of threads to create to handle TCP connections "
                        "to this node's external API.",
                        "3");
  node.tagParameter("api_worker_pool_size", defaultableByReplicaTags);
  node.addValidator("api_worker_pool_size", validatePositiveReplicaInt,
                    nullptr);

  node.declareParameter("blockchain_db_impl",
                        "Database implementation to be used by this replica to "
                        "persist blockchain state.",
                        "rocksdb");
  node.tagParameter("blockchain_db_impl", defaultableByReplicaTags);
  node.addValidator("blockchain_db_impl", validateDatabaseImplementation,
                    nullptr);

  node.declareParameter(
      "blockchain_db_path",
      "Path to storage to use to persist blockchain data for this replica "
      "using the database implementation specified by blockchain_db_impl.",
      "rocksdbdata");
  node.tagParameter("blockchain_db_path", defaultableByReplicaTags);

  node.declareParameter(
      "concord-bft_enable_debug_statistics",
      "If set to true, Concord-BFT will periodically log debug statistics for "
      "this Concord node, such as throughput metrics and number of messages "
      "sent/received.",
      "false");
  node.tagParameter("concord-bft_enable_debug_statistics",
                    defaultableByReplicaTags);
  node.addValidator("concord-bft_enable_debug_statistics", validateBoolean,
                    nullptr);

  node.declareParameter(
      "genesis_block",
      "Path, in the node's local filesystem, to a JSON file containing the "
      "genesis block data for this blockchain.");
  node.tagParameter("genesis_block", privateOptionalTags);

  node.declareParameter(
      "logger_config",
      "Path, in this node's local filesystem to a configuration for Log4CPlus, "
      "the logging framework Concord uses.",
      "/concord/resources/log4cplus.properties");
  node.tagParameter("logger_config", defaultableByReplicaTags);

  node.declareParameter(
      "logger_reconfig_time",
      "Interval, measured in milliseconds, with which this replica should "
      "check the file specified by logger_config for changes in requested "
      "logging behavior.",
      "60000");
  node.tagParameter("logger_reconfig_time", defaultableByReplicaTags);
  node.addValidator("logger_reconfig_time", validatePositiveReplicaInt,
                    nullptr);

  node.declareParameter(
      "jaeger_agent",
      "Host:Port of the jaeger-agent process to receive traces "
      "(127.0.0.1:6831 by default).");
  node.tagParameter("jaeger_agent", privateOptionalTags);

  node.declareParameter("prometheus_port",
                        "Port of prometheus client to publish metrics on "
                        "(9891 by default).");
  node.tagParameter("prometheus_port", privateOptionalTags);

  node.declareParameter("dump_metrics_interval_sec",
                        "Time interval for dumping concord metrics to log "
                        "(600 seconds by default).");
  node.tagParameter("dump_metrics_interval_sec", privateOptionalTags);

  node.declareParameter("preexec_requests_status_check_period_millisec",
                        "Time interval for a periodic detection of timed out "
                        "pre-execution requests "
                        "(5000 milliseconds by default).",
                        "5000");
  node.tagParameter("preexec_requests_status_check_period_millisec",
                    privateOptionalTags);
  node.addValidator("preexec_requests_status_check_period_millisec",
                    validatePositiveReplicaInt, nullptr);

  node.declareParameter("metrics_config",
                        "Path, in this node's local filesystem, to a "
                        "configuration for concord metrics",
                        "/concord/resources/metrics_config.yaml");
  node.tagParameter("metrics_config", defaultableByReplicaTags);

  node.declareParameter("service_host",
                        "Public IP address or hostname on which this replica's "
                        "external API service can be reached.");
  node.tagParameter("service_host", privateInputTags);

  node.declareParameter(
      "service_port",
      "Port on which this replica's external API service can be reached.");
  node.tagParameter("service_port", privateInputTags);
  node.addValidator("service_port", validatePortNumber, nullptr);

  node.declareParameter("bft_metrics_udp_port",
                        "Port for reading BFT metrics (JSON payload) via UDP.");
  node.tagParameter("bft_metrics_udp_port", privateOptionalTags);
  node.addValidator("bft_metrics_udp_port", validatePortNumber, nullptr);

  node.declareParameter(
      "transaction_list_max_count",
      "Maximum number of transactions to allow this replica to return to "
      "queries to its public API service requesting lists of transactions.",
      "10");
  node.tagParameter("transaction_list_max_count", defaultableByReplicaTags);
  node.addValidator("transaction_list_max_count", validatePositiveReplicaInt,
                    nullptr);

  node.declareParameter(
      "time_source_id",
      "The source name `time-sourceX` is based on the node index."
      "Ignored unless FEATURE_time_service is \"true\".");
  node.tagParameter("time_source_id", publicGeneratedTags);
  node.addGenerator("time_source_id", computeTimeSourceId, nullptr);

  node.declareParameter(
      "time_pusher_period_ms",
      "How often a node should guarantee that its time is published, in "
      "milliseconds. Ignored unless FEATURE_time_service is \"true\", and "
      "time_source_id is given.");
  node.tagParameter("time_pusher_period_ms", publicOptionalTags);
  node.addValidator(
      "time_pusher_period_ms", validateInt,
      const_cast<void*>(reinterpret_cast<const void*>(&kInt32Limits)));

  replica.declareParameter("commit_private_key",
                           "Private key for this replica under the general "
                           "case commit cryptosystem.");
  replica.tagParameter("commit_private_key", privateGeneratedTags);
  replica.addValidator("commit_private_key", validatePrivateKey,
                       &(auxState->commitCryptosys));
  replica.addGenerator("commit_private_key", getThresholdPrivateKey,
                       &(auxState->commitCryptosys));

  replica.declareParameter(
      "commit_verification_key",
      "Public verification key for this replica's signature under the general "
      "case commit cryptosystem.");
  replica.tagParameter("commit_verification_key", publicGeneratedTags);
  replica.addValidator("commit_verification_key", validateVerificationKey,
                       &(auxState->commitCryptosys));
  replica.addGenerator("commit_verification_key", getThresholdVerificationKey,
                       &(auxState->commitCryptosys));

  replica.declareParameter("optimistic_commit_private_key",
                           "Private key for this replica under the optimistic "
                           "fast path commit cryptosystem.");
  replica.tagParameter("optimistic_commit_private_key", privateGeneratedTags);
  replica.addValidator("optimistic_commit_private_key", validatePrivateKey,
                       &(auxState->optimisticCommitCryptosys));
  replica.addGenerator("optimistic_commit_private_key", getThresholdPrivateKey,
                       &(auxState->optimisticCommitCryptosys));

  replica.declareParameter(
      "optimistic_commit_verification_key",
      "Public verification key for this replica's signature under the "
      "optimistic fast path commit cryptosystem.");
  replica.tagParameter("optimistic_commit_verification_key",
                       publicGeneratedTags);
  replica.addValidator("optimistic_commit_verification_key",
                       validateVerificationKey,
                       &(auxState->optimisticCommitCryptosys));
  replica.addGenerator("optimistic_commit_verification_key",
                       getThresholdVerificationKey,
                       &(auxState->optimisticCommitCryptosys));

  replica.declareParameter(
      "principal_id",
      "Unique ID number for this Concord-BFT replica. Concord-BFT considers "
      "replicas and client proxies to be principals, each of which must have a "
      "unique ID.");
  replica.tagParameter("principal_id", publicGeneratedTags);
  replica.addValidator("principal_id", validatePrincipalId, nullptr);
  replica.addGenerator("principal_id", computePrincipalId, nullptr);

  replica.declareParameter(
      "private_key",
      "RSA private key for this replica to use for general communication.");
  replica.tagParameter("private_key", privateGeneratedTags);
  replica.addValidator("private_key", validateRSAPrivateKey, nullptr);
  replica.addGenerator("private_key", getRSAPrivateKey, nullptr);

  replica.declareParameter(
      "public_key",
      "RSA public key corresponding to this replica's RSA private key.");
  replica.tagParameter("public_key", publicGeneratedTags);
  replica.addValidator("public_key", validateRSAPublicKey, nullptr);
  replica.addGenerator("public_key", getRSAPublicKey, nullptr);

  replica.declareParameter(
      "replica_host",
      "Public IP address or host name with which other replicas can reach this "
      "one for consensus communication.");
  replica.tagParameter("replica_host", principalHostTags);
  replica.addValidator("replica_host", validatePrincipalHost, nullptr);

  replica.declareParameter("replica_port",
                           "Port number on which other replicas can reach this "
                           "one for consensus communication.");
  replica.tagParameter("replica_port", publicInputTags);
  replica.addValidator("replica_port", validatePortNumber, nullptr);

  replica.declareParameter(
      "slow_commit_private_key",
      "Private key for this replica under the slow path commit cryptosystem.");
  replica.tagParameter("slow_commit_private_key", privateGeneratedTags);
  replica.addValidator("slow_commit_private_key", validatePrivateKey,
                       &(auxState->slowCommitCryptosys));
  replica.addGenerator("slow_commit_private_key", getThresholdPrivateKey,
                       &(auxState->slowCommitCryptosys));

  replica.declareParameter(
      "slow_commit_verification_key",
      "Public verification key for this replica's signature under the slow "
      "path commit cryptosystem.");
  replica.tagParameter("slow_commit_verification_key", publicGeneratedTags);
  replica.addValidator("slow_commit_verification_key", validateVerificationKey,
                       &(auxState->slowCommitCryptosys));
  replica.addGenerator("slow_commit_verification_key",
                       getThresholdVerificationKey,
                       &(auxState->slowCommitCryptosys));

  clientProxy.declareParameter("client_host",
                               "Public IP address or host name with which this "
                               "client proxy can be reached.");
  clientProxy.tagParameter("client_host", principalHostTags);
  clientProxy.addValidator("client_host", validatePrincipalHost, nullptr);

  clientProxy.declareParameter(
      "client_port", "Port on which this client proxy can be reached.");
  clientProxy.tagParameter("client_port", publicInputTags);
  clientProxy.addValidator("client_port", validatePortNumber, nullptr);

  clientProxy.declareParameter(
      "principal_id",
      "Unique ID number for client proxy. Concord-BFT considers both replicas "
      "and client proxies to be principals, and it requires all principals "
      "have a unique ID.");
  clientProxy.tagParameter("principal_id", publicGeneratedTags);
  clientProxy.addValidator("principal_id", validatePrincipalId, nullptr);
  clientProxy.addGenerator("principal_id", computePrincipalId, nullptr);

  // TLS
  config.declareParameter("tls_cipher_suite_list",
                          "TLS cipher suite list to use");
  config.tagParameter("tls_cipher_suite_list", publicInputTags);
  config.declareParameter("tls_certificates_folder_path",
                          "TLS certificates root folder path");
  config.tagParameter("tls_certificates_folder_path", publicInputTags);
  config.declareParameter("comm_to_use", "Default communication module");
  config.tagParameter("comm_to_use", publicInputTags);
}

void loadClusterSizeParameters(YAMLConfigurationInput& input,
                               ConcordConfiguration& config) {
  Logger logger = Logger::getInstance("com.vmware.concord.configuration");

  ConfigurationPath fValPath("f_val", false);
  ConfigurationPath cValPath("c_val", false);
  ConfigurationPath clientProxiesPerReplicaPath("client_proxies_per_replica",
                                                false);
  vector<ConfigurationPath> requiredParameters(
      {fValPath, cValPath, clientProxiesPerReplicaPath});

  input.loadConfiguration(config, requiredParameters.begin(),
                          requiredParameters.end(), &logger, true);

  bool missingValue = false;
  for (auto&& parameter : requiredParameters) {
    if (!config.hasValue<uint16_t>(parameter)) {
      missingValue = true;
      LOG4CPLUS_ERROR(
          logger, "Value not found for required cluster sizing parameter: " +
                      parameter.toString());
    }
  }
  if (missingValue) {
    throw ConfigurationResourceNotFoundException(
        "Cannot load cluster size parameters: missing required cluster size "
        "parameter.");
  }
}

// Functions used by instantiateTemplatedConfiguration to select subsets of
// parameter paths in order to correctly load template contents before
// instantiating templates.
static bool selectStrictlyTemplatedParameters(
    const ConcordConfiguration& config, const ConfigurationPath& path,
    void* state) {
  if (!path.isScope || path.useInstance) {
    return false;
  }
  const ConfigurationPath* pathStep = &path;
  while (pathStep->isScope && pathStep->subpath) {
    if (pathStep->useInstance) {
      return false;
    }
    pathStep = pathStep->subpath.get();
  }
  return true;
}

static bool selectTemplatedInstancedParameters(
    const ConcordConfiguration& config, const ConfigurationPath& path,
    void* state) {
  return (path.isScope && !path.useInstance && path.subpath &&
          path.subpath->isScope && path.subpath->useInstance &&
          path.subpath->subpath && !(path.subpath->subpath->isScope));
}

static bool selectInstancedTemplatedParameters(
    const ConcordConfiguration& config, const ConfigurationPath& path,
    void* state) {
  return (path.isScope && path.useInstance && path.subpath &&
          path.subpath->isScope && !(path.subpath->useInstance) &&
          path.subpath->subpath && !(path.subpath->subpath->isScope));
}

static bool selectInstancedInstancedParameters(
    const ConcordConfiguration& config, const ConfigurationPath& path,
    void* state) {
  return (path.isScope && path.useInstance && path.subpath &&
          path.subpath->isScope && path.subpath->useInstance &&
          path.subpath->subpath && !(path.subpath->subpath->isScope));
}

void instantiateTemplatedConfiguration(YAMLConfigurationInput& input,
                                       ConcordConfiguration& config) {
  Logger logger = Logger::getInstance("com.vmware.concord.configuration");

  if (!config.hasValue<uint16_t>("f_val") ||
      !config.hasValue<uint16_t>("c_val") ||
      !config.hasValue<uint16_t>("client_proxies_per_replica")) {
    throw ConfigurationResourceNotFoundException(
        "Cannot instantiate scopes for Concord configuration: required cluster "
        "size parameters are not loaded.");
  }

  assert(config.containsScope("node"));
  ConcordConfiguration& node = config.subscope("node");
  assert(node.containsScope("replica"));
  assert(node.containsScope("client_proxy"));

  // Note this function is complicated by the fact that it handles loading the
  // contents of templates before instantiating them as well as the fact that
  // the input could contain parameters in a mixed state of being template or
  // instance parameters (for example, the input could give a value of the port
  // number for the first client proxy on each node).

  // First, load any parameters in purely templated scopes, then instantiate the
  // scopes within the node template.
  ParameterSelection selection(config, selectStrictlyTemplatedParameters,
                               nullptr);
  input.loadConfiguration(config, selection.begin(), selection.end(), &logger,
                          true);
  node.instantiateScope("replica");
  node.instantiateScope("client_proxy");

  // Next, load values for parameters in instances of scopes within the node
  // template. After that node can be instantiated.
  selection =
      ParameterSelection(config, selectTemplatedInstancedParameters, nullptr);
  input.loadConfiguration(config, selection.begin(), selection.end(), &logger,
                          true);
  config.instantiateScope("node");

  // Now, we load values to parameters in scope templates within node instances.
  selection =
      ParameterSelection(config, selectInstancedTemplatedParameters, nullptr);
  input.loadConfiguration(config, selection.begin(), selection.end(), &logger,
                          true);

  // Finally, to enforce the policy that explicit instanced parameter
  // specifications override values from their templates, we traverse the set of
  // parameters that are contained in errrinstances of scopes within node
  // instances, and write to them any values their node instance's template has
  // for the same parameter.
  selection =
      ParameterSelection(config, selectInstancedInstancedParameters, nullptr);
  for (auto iterator = selection.begin(); iterator != selection.end();
       ++iterator) {
    ConfigurationPath instancePath = *iterator;
    ConfigurationPath templatePath(instancePath);
    templatePath.subpath->useInstance = false;
    ConfigurationPath containingScopeOfInstancePath(instancePath);
    containingScopeOfInstancePath.subpath->subpath.reset();

    if (config.hasValue<string>(templatePath)) {
      string value = config.getValue<string>(templatePath);
      ConcordConfiguration& subscope =
          config.subscope(containingScopeOfInstancePath);
      string failureMessage;
      if (subscope.loadValue(instancePath.subpath->subpath->name, value,
                             &failureMessage, true) ==
          ConcordConfiguration::ParameterStatus::INVALID) {
        LOG4CPLUS_ERROR(logger, "Cannot load value " + value +
                                    " to parameter " + instancePath.toString() +
                                    ": " + failureMessage);
      }
    }
  }
}

// Helper function used in error reporting by a number of configuration-loading
// functions that can possibly throw exceptions reporting multiple missing
// parameters at once; it may be helpful to list them all in the exception
// message instead of or in addition to logging each missing parameter
// individually as it can be possible for log statements to get loast from the
// output if they do not complete and their message does not get flushed to the
// output stream(s) before an exception following them triggers the program to
// exit.
static string getErrorMessageListingParameters(
    const string& base_error_message,
    const vector<string>& parameters_missing) {
  string error_message = base_error_message;
  for (size_t i = 0; i < parameters_missing.size(); ++i) {
    error_message += parameters_missing[i];
    if (i < (parameters_missing.size() - 1)) {
      error_message += ", ";
    } else {
      error_message += ".";
    }
  }
  return error_message;
}

// Parameter selection function used by loadConfigurationInputParameters.
static bool selectInputParameters(const ConcordConfiguration& config,
                                  const ConfigurationPath& path, void* state) {
  assert(config.contains(path));
  const ConcordConfiguration* containingScope = &config;
  if (path.isScope) {
    containingScope = &(config.subscope(path.trimLeaf()));
  }
  string name = path.getLeaf().name;

  return containingScope->isTagged(name, "input") ||
         containingScope->isTagged(name, "defaultable") ||
         containingScope->isTagged(name, "optional");
}

void loadConfigurationInputParameters(YAMLConfigurationInput& input,
                                      ConcordConfiguration& config) {
  Logger logger = Logger::getInstance("com.vmware.concord.configuration");

  ParameterSelection inputParameterSelection(config, selectInputParameters,
                                             nullptr);
  input.loadConfiguration(config, inputParameterSelection.begin(),
                          inputParameterSelection.end(), &logger, true);

  bool missingParameter = false;
  vector<string> parameters_missing;
  for (auto iterator =
           config.begin(ConcordConfiguration::kIterateAllInstanceParameters);
       iterator !=
       config.end(ConcordConfiguration::kIterateAllInstanceParameters);
       ++iterator) {
    ConfigurationPath path = *iterator;
    const ConcordConfiguration* containingScope = &config;
    if (path.isScope) {
      containingScope = &(config.subscope(path.trimLeaf()));
    }
    string name = path.getLeaf().name;
    if (containingScope->isTagged(name, "input") &&
        !config.hasValue<string>(path)) {
      missingParameter = true;
      parameters_missing.push_back(path.toString());
      LOG4CPLUS_ERROR(logger,
                      "Configuration input is missing value for required input "
                      "parameter: " +
                          path.toString());
    }
  }
  if (missingParameter) {
    throw ConfigurationResourceNotFoundException(
        getErrorMessageListingParameters(
            "Configuration input is missing required input parameter(s): ",
            parameters_missing));
  }
}

void generateConfigurationKeys(ConcordConfiguration& config) {
  Logger logger = Logger::getInstance("com.vmware.concord.configuration");

  if (!config.hasValue<uint16_t>("f_val") ||
      !config.hasValue<uint16_t>("c_val") ||
      !config.hasValue<uint16_t>("client_proxies_per_replica")) {
    throw ConfigurationResourceNotFoundException(
        "Cannot generate keys for Concord cluster: required cluster size "
        "parameters are not loaded.");
  }
  if (!config.hasValue<string>("slow_commit_cryptosys") ||
      !config.hasValue<string>("commit_cryptosys") ||
      !config.hasValue<string>("optimistic_commit_cryptosys")) {
    throw ConfigurationResourceNotFoundException(
        "Cannot generate keys for Concord cluster: required cryptosystem "
        "selections have not been loaded.");
  }

  // Although the validators for these cryptosystem selections should have been
  // run when the values were loaded for them, we check that the validators
  // accept the values again here in case any of them were previously unable to
  // fully validate a cryptosystem selection because cryptosystem selections
  // were loaded before cluster size parameters.
  if ((config.validate("slow_commit_cryptosys") !=
       ConcordConfiguration::ParameterStatus::VALID) ||
      (config.validate("commit_cryptosys") !=
       ConcordConfiguration::ParameterStatus::VALID) ||
      (config.validate("optimistic_commit_cryptosys") !=
       ConcordConfiguration::ParameterStatus::VALID)) {
    throw ConfigurationResourceNotFoundException(
        "Cannot generate keys for Concord cluster: a cryptosystem selection is "
        "not valid.");
  }
  uint16_t fVal = config.getValue<uint16_t>("f_val");
  uint16_t cVal = config.getValue<uint16_t>("c_val");

  uint16_t numReplicas = 3 * fVal + 2 * cVal + 1;

  uint16_t numSigners = numReplicas;
  uint16_t slowCommitThreshold = 2 * fVal + cVal + 1;
  uint16_t commitThreshold = 3 * fVal + cVal + 1;
  uint16_t optimisticCommitThreshold = 3 * fVal + 2 * cVal + 1;

  assert(config.getAuxiliaryState());
  ConcordPrimaryConfigurationAuxiliaryState* auxState =
      dynamic_cast<ConcordPrimaryConfigurationAuxiliaryState*>(
          config.getAuxiliaryState());

  std::pair<string, string> slowCommitCryptoSelection =
      parseCryptosystemSelection(
          config.getValue<string>("slow_commit_cryptosys"));
  std::pair<string, string> commitCryptoSelection =
      parseCryptosystemSelection(config.getValue<string>("commit_cryptosys"));
  std::pair<string, string> optimisticCommitCryptoSelection =
      parseCryptosystemSelection(
          config.getValue<string>("optimistic_commit_cryptosys"));

  auxState->slowCommitCryptosys.reset(new Cryptosystem(
      slowCommitCryptoSelection.first, slowCommitCryptoSelection.second,
      numSigners, slowCommitThreshold));
  auxState->commitCryptosys.reset(new Cryptosystem(
      commitCryptoSelection.first, commitCryptoSelection.second, numSigners,
      commitThreshold));
  auxState->optimisticCommitCryptosys.reset(
      new Cryptosystem(optimisticCommitCryptoSelection.first,
                       optimisticCommitCryptoSelection.second, numSigners,
                       optimisticCommitThreshold));

  LOG4CPLUS_INFO(logger,
                 "Generating threshold cryptographic keys for slow path commit "
                 "cryptosystem...");
  auxState->slowCommitCryptosys->generateNewPseudorandomKeys();
  LOG4CPLUS_INFO(
      logger,
      "Generating threshold cryptographic keys for commit cryptosystem...");
  auxState->commitCryptosys->generateNewPseudorandomKeys();
  LOG4CPLUS_INFO(logger,
                 "Generating threshold cryptographic keys for optimistic fast "
                 "path commit cryptosystem...");
  auxState->optimisticCommitCryptosys->generateNewPseudorandomKeys();

  auxState->replicaRSAKeys.clear();

  LOG4CPLUS_INFO(logger, "Generating Concord-BFT replica RSA keys...");
  CryptoPP::AutoSeededRandomPool randomPool;
  for (uint16_t i = 0; i < numReplicas; ++i) {
    auxState->replicaRSAKeys.push_back(generateRSAKeyPair(randomPool));
  }
}

static bool selectParametersRequiredAtConfigurationGeneration(
    const ConcordConfiguration& config, const ConfigurationPath& path,
    void* state) {
  // The configuration generator does not output tempalted parameters, as it
  // outputs specific values for each instance of a parameter.
  const ConfigurationPath* pathStep = &path;
  while (pathStep->isScope && pathStep->subpath) {
    if (!(pathStep->useInstance)) {
      return false;
    }
    pathStep = pathStep->subpath.get();
  }

  const ConcordConfiguration* containingScope = &config;
  if (path.isScope) {
    containingScope = &(config.subscope(path.trimLeaf()));
  }
  return containingScope->isTagged(path.getLeaf().name,
                                   "config_generation_time");
}

bool hasAllParametersRequiredAtConfigurationGeneration(
    ConcordConfiguration& config) {
  Logger logger = Logger::getInstance("com.vmware.concord.configuration");

  ParameterSelection requiredConfiguration(
      config, selectParametersRequiredAtConfigurationGeneration, nullptr);

  bool hasAllRequired = true;
  for (auto iterator = requiredConfiguration.begin();
       iterator != requiredConfiguration.end(); ++iterator) {
    ConfigurationPath path = *iterator;
    if (!config.hasValue<string>(path)) {
      LOG4CPLUS_ERROR(logger,
                      "Missing value for required configuration parameter: " +
                          path.toString() + ".");
      hasAllRequired = false;
    }
  }
  return hasAllRequired;
}

static bool selectHostsToMakeLoopback(const ConcordConfiguration& config,
                                      const ConfigurationPath& path,
                                      void* state) {
  assert(state);
  size_t node = *(static_cast<size_t*>(state));

  if (!path.isScope || !path.subpath || !path.useInstance ||
      (path.index != node)) {
    return false;
  }

  const ConcordConfiguration& containing_scope =
      config.subscope(path.trimLeaf());
  return containing_scope.isTagged(path.getLeaf().name, "could_be_loopback");
}

static bool selectNodeConfiguration(const ConcordConfiguration& config,
                                    const ConfigurationPath& path,
                                    void* state) {
  assert(state);

  // The configuration generator does not output tempalted parameters, as it
  // outputs specific values for each instance of a parameter.
  const ConfigurationPath* pathStep = &path;
  while (pathStep->isScope && pathStep->subpath) {
    if (!(pathStep->useInstance)) {
      return false;
    }
    pathStep = pathStep->subpath.get();
  }

  size_t node = *(static_cast<size_t*>(state));

  if (path.isScope && (path.name == "node") && path.useInstance &&
      (path.index == node)) {
    return true;
  }

  const ConcordConfiguration* containingScope = &config;
  if (path.isScope) {
    containingScope = &(config.subscope(path.trimLeaf()));
  }

  return !(containingScope->isTagged(path.getLeaf().name, "private"));
}

void outputConcordNodeConfiguration(const ConcordConfiguration& config,
                                    YAMLConfigurationOutput& output,
                                    size_t node) {
  Logger logger = Logger::getInstance("com.vmware.concord.configuration");
  ConcordConfiguration node_config = config;
  if (config.hasValue<bool>("use_loopback_for_local_hosts") &&
      config.getValue<bool>("use_loopback_for_local_hosts")) {
    ParameterSelection node_local_hosts(node_config, selectHostsToMakeLoopback,
                                        &(node));
    for (auto& path : node_local_hosts) {
      ConcordConfiguration* containing_scope = &node_config;
      if (path.isScope && path.subpath) {
        containing_scope = &(node_config.subscope(path.trimLeaf()));
      }
      string failure_message;
      if (containing_scope->loadValue(path.getLeaf().name, "127.0.0.1",
                                      &failure_message, true) ==
          ConcordConfiguration::ParameterStatus::INVALID) {
        throw invalid_argument("Failed to load 127.0.0.1 for host " +
                               path.toString() + " for node " +
                               to_string(node) +
                               "\'s configuration: " + failure_message);
      }
    }
  }
  ParameterSelection node_config_params(node_config, selectNodeConfiguration,
                                        &(node));
  output.outputConfiguration(node_config, node_config_params.begin(),
                             node_config_params.end());
}

void loadNodeConfiguration(ConcordConfiguration& config,
                           YAMLConfigurationInput& input) {
  Logger logger = Logger::getInstance("com.vmware.concord.configuration");

  loadClusterSizeParameters(input, config);
  instantiateTemplatedConfiguration(input, config);

  // Note there are currently no configuration parameters that cannot be loaded
  // from the node's configuration file (for example, in the future, this could
  // include generated parameters that must be generated on the nodes and are
  // therefore unaccptable as input from the node's configuration files). If
  // this changes in the future, the immediately following
  // YAMLConfigurationInput::loadConfiguration call will need to be adjusted to
  // use a ParameterSelection that excludes such parameters.
  input.loadConfiguration(
      config, config.begin(ConcordConfiguration::kIterateAllInstanceParameters),
      config.end(ConcordConfiguration::kIterateAllInstanceParameters), &logger);

  size_t localNode = detectLocalNode(config);
  ParameterSelection nodeConfiguration(config, selectNodeConfiguration,
                                       &localNode);

  // Try loading defaults and running generators for optional parameters and
  // parameters that can be implicit in the node configuration, excluding those
  // tagged "config_generation_time" (whose values must be settled on at
  // configuration generation time and therefore cannot be picked here by the
  // booting Concord node).
  for (auto iterator = nodeConfiguration.begin();
       iterator != nodeConfiguration.end(); ++iterator) {
    ConfigurationPath path = *iterator;
    ConcordConfiguration* containingScope = &config;
    if (path.isScope && path.subpath) {
      containingScope = &(config.subscope(path.trimLeaf()));
    }
    string name = path.getLeaf().name;

    if (!(containingScope->hasValue<string>(name)) &&
        !(containingScope->isTagged(name, "config_generation_time"))) {
      if (containingScope->isTagged(name, "defaultable")) {
        containingScope->loadDefault(name);
      } else if (containingScope->isTagged(name, "generated")) {
        string failureMessage;
        if (containingScope->generate(name, &failureMessage) !=
            ConcordConfiguration::ParameterStatus::VALID) {
          LOG4CPLUS_ERROR(logger, "Cannot generate value for " +
                                      path.toString() + ": " + failureMessage);
        }
      }
    }
  }

  // Validate that all parameters the node will need have actually been loaded
  // and that none have invalid valus. We currently choose not to reject the
  // configuration if some validators claim insufficient information in case
  // some validators actually account for private information from other nodes
  // in their validation.
  if (config.validateAll(true, false) ==
      ConcordConfiguration::ParameterStatus::INVALID) {
    LOG4CPLUS_ERROR(logger,
                    "Node configuration was found to contain some invalid "
                    "value(s) on final validation.");
    throw ConfigurationResourceNotFoundException(
        "Node configuration complete validation failed.");
  }

  bool hasAllRequired = true;
  vector<string> parameters_missing;
  for (auto iterator = nodeConfiguration.begin();
       iterator != nodeConfiguration.end(); ++iterator) {
    ConfigurationPath path = *iterator;
    if (!config.hasValue<string>(path)) {
      ConcordConfiguration* containingScope = &config;
      if (path.isScope && path.subpath) {
        containingScope = &(config.subscope(path.trimLeaf()));
      }
      if (!(containingScope->isTagged(path.getLeaf().name, "optional"))) {
        hasAllRequired = false;
        parameters_missing.push_back((*iterator).toString());
        LOG4CPLUS_ERROR(logger,
                        "Concord node configuration is missing a value for a "
                        "required parameter: " +
                            (*iterator).toString());
      }
    }
  }
  if (!hasAllRequired) {
    throw ConfigurationResourceNotFoundException(
        getErrorMessageListingParameters("Node configuration is missing "
                                         "value(s) for required parameter(s): ",
                                         parameters_missing));
  }
}

size_t detectLocalNode(ConcordConfiguration& config) {
  size_t nodeDetected;
  bool hasDetectedNode = false;
  ConfigurationPath detectedPath;

  bool hasValueForAnyNodePublicParameter = false;
  bool hasValueForAnyNodeTemplateParameter = false;
  bool hasValueForAnyNonNodeParameter = false;

  for (auto iterator =
           config.begin(ConcordConfiguration::kIterateAllParameters);
       iterator != config.end(ConcordConfiguration::kIterateAllParameters);
       ++iterator) {
    ConfigurationPath path = *iterator;
    if (path.isScope && (path.name == "node")) {
      if (path.useInstance) {
        size_t node = path.index;
        ConcordConfiguration* containingScope =
            &(config.subscope(path.trimLeaf()));
        if (containingScope->isTagged(path.getLeaf().name, "private") &&
            config.hasValue<string>(path)) {
          if (hasDetectedNode && (node != nodeDetected)) {
            throw ConfigurationResourceNotFoundException(
                "Cannot determine which node Concord configuration file is "
                "for: found values for private configuration parameters for "
                "multiple nodes. Conflicting private parameters are : " +
                detectedPath.toString() + " and " + path.toString() + ".");
          }
          hasDetectedNode = true;
          nodeDetected = node;
          detectedPath = path;
        } else if (config.hasValue<string>(path)) {
          hasValueForAnyNodePublicParameter = true;
        }
      } else {
        if (config.hasValue<string>(path)) {
          hasValueForAnyNodeTemplateParameter = true;
        }
      }
    } else {
      if (config.hasValue<string>(path)) {
        hasValueForAnyNonNodeParameter = true;
      }
    }
  }

  if (!hasDetectedNode) {
    if (hasValueForAnyNodePublicParameter) {
      throw ConfigurationResourceNotFoundException(
          "Cannot determine which node configuration file is for: no values "
          "found for any private configuration parameters in instances of the "
          "node scope.");
    } else if (hasValueForAnyNodeTemplateParameter) {
      throw ConfigurationResourceNotFoundException(
          "Cannot determine which node configuration file is for: no values "
          "found for any parameters in instances of the node scope, though "
          "there are values for parameters in the node template.");
    } else if (hasValueForAnyNonNodeParameter) {
      throw ConfigurationResourceNotFoundException(
          "Cannot determine which node configuration file is for: no values "
          "found for any parameters in the node scope (Is the node scope "
          "missing or malformatted in Concord's configuration file?).");
    } else {
      throw ConfigurationResourceNotFoundException(
          "Cannot determine which node configuration file is for: no values "
          "found for any recognized parameters in Concord's configuration "
          "file. (Has Concord been given the wrong file for its configuration? "
          "Is the configuration file malformatted?)");
    }
  }
  return nodeDetected;
}

void loadSBFTCryptosystems(ConcordConfiguration& config) {
  Logger logger = Logger::getInstance("com.vmware.concord.configuration");

  // Note we do not validate that the cryptosystem selections here are valid as
  // long as they exist, as we expect this has been handled by the parameter
  // validators as the configuration was loaded.
  vector<ConfigurationPath> requiredCryptosystemParameters(
      {ConfigurationPath("f_val", false), ConfigurationPath("c_val", false),
       ConfigurationPath("slow_commit_cryptosys", false),
       ConfigurationPath("commit_cryptosys", false),
       ConfigurationPath("optimistic_commit_cryptosys", false),
       ConfigurationPath("slow_commit_public_key", false),
       ConfigurationPath("commit_public_key", false),
       ConfigurationPath("optimistic_commit_public_key", false)});
  bool hasRequired = true;
  vector<string> parameters_missing;
  for (auto&& path : requiredCryptosystemParameters) {
    if (!config.hasValue<string>(path)) {
      hasRequired = false;
      parameters_missing.push_back(path.toString());
      LOG4CPLUS_ERROR(
          logger,
          "Configuration missing value for required cryptosystem parameter: " +
              path.toString());
    }
  }
  if (!hasRequired) {
    throw ConfigurationResourceNotFoundException(
        getErrorMessageListingParameters(
            "Cannot load SBFT Cryptosystems for given configuration: "
            "configuration is missing value(s) for required crypto "
            "parameter(s): ",
            parameters_missing));
  }

  ConcordPrimaryConfigurationAuxiliaryState* auxState;
  auxState = dynamic_cast<ConcordPrimaryConfigurationAuxiliaryState*>(
      config.getAuxiliaryState());

  assert(auxState);

  uint16_t fVal = config.getValue<uint16_t>("f_val");
  uint16_t cVal = config.getValue<uint16_t>("c_val");
  uint16_t slowCommitThresh = 2 * fVal + cVal + 1;
  uint16_t commitThresh = 3 * fVal + cVal + 1;
  uint16_t optimisticCommitThresh = 3 * fVal + 2 * cVal + 1;
  uint16_t numSigners = 3 * fVal + 2 * cVal + 1;

  std::pair<string, string> slowCommitCryptoSelection =
      parseCryptosystemSelection(
          config.getValue<string>("slow_commit_cryptosys"));
  std::pair<string, string> commitCryptoSelection =
      parseCryptosystemSelection(config.getValue<string>("commit_cryptosys"));
  std::pair<string, string> optimisticCommitCryptoSelection =
      parseCryptosystemSelection(
          config.getValue<string>("optimistic_commit_cryptosys"));

  auxState->slowCommitCryptosys.reset(new Cryptosystem(
      slowCommitCryptoSelection.first, slowCommitCryptoSelection.second,
      numSigners, slowCommitThresh));
  auxState->commitCryptosys.reset(new Cryptosystem(commitCryptoSelection.first,
                                                   commitCryptoSelection.second,
                                                   numSigners, commitThresh));
  auxState->optimisticCommitCryptosys.reset(
      new Cryptosystem(optimisticCommitCryptoSelection.first,
                       optimisticCommitCryptoSelection.second, numSigners,
                       optimisticCommitThresh));

  // Note these vectors will be given to Cryptosystems, which consider them to
  // be 1-indexed.
  vector<string> slowCommitVerificationKeys(numSigners + 1);
  vector<string> commitVerificationKeys(numSigners + 1);
  vector<string> optimisticCommitVerificationKeys(numSigners + 1);

  assert(config.containsScope("node") && config.scopeIsInstantiated("node") &&
         (config.scopeSize("node") == numSigners));
  for (uint16_t i = 0; i < numSigners; ++i) {
    ConcordConfiguration& nodeConfig = config.subscope("node", i);
    assert(nodeConfig.containsScope("replica") &&
           nodeConfig.scopeIsInstantiated("replica") &&
           (nodeConfig.scopeSize("replica") == 1));
    ConcordConfiguration& replicaConfig = nodeConfig.subscope("replica", 0);
    uint16_t replicaID = replicaConfig.getValue<uint16_t>("principal_id");

    if (!replicaConfig.hasValue<string>("slow_commit_verification_key")) {
      hasRequired = false;
      parameters_missing.push_back("node[" + to_string(i) +
                                   "]/replica[0]/slow_commit_verification_key");
      LOG4CPLUS_ERROR(logger,
                      "Configuration missing required threshold verification "
                      "key: slow_commit_verification_key for replica " +
                          to_string(i) + ".");
    } else {
      slowCommitVerificationKeys[replicaID + 1] =
          replicaConfig.getValue<string>("slow_commit_verification_key");
    }
    if (!replicaConfig.hasValue<string>("commit_verification_key")) {
      hasRequired = false;
      parameters_missing.push_back("node[" + to_string(i) +
                                   "]/replica[0]/commit_verification_key");
      LOG4CPLUS_ERROR(logger,
                      "Configuration missing required threshold verification "
                      "key: commit_verification_key for replica " +
                          to_string(i) + ".");
    } else {
      commitVerificationKeys[replicaID + 1] =
          replicaConfig.getValue<string>("commit_verification_key");
    }
    if (!replicaConfig.hasValue<string>("optimistic_commit_verification_key")) {
      hasRequired = false;
      parameters_missing.push_back(
          "node[" + to_string(i) +
          "]/replica[0]/optimistic_commit_verification_key");
      LOG4CPLUS_ERROR(logger,
                      "Configuration missing required threshold verification "
                      "key: optimistic_commit_verification_key for replica " +
                          to_string(i) + ".");
    } else {
      optimisticCommitVerificationKeys[replicaID + 1] =
          replicaConfig.getValue<string>("optimistic_commit_verification_key");
    }
  }
  if (!hasRequired) {
    throw ConfigurationResourceNotFoundException(
        getErrorMessageListingParameters(
            "Cannot load SBFT Cryptosystems: configuration is missing value(s) "
            "for required parameter(s): ",
            parameters_missing));
  }

  auxState->slowCommitCryptosys->loadKeys(
      config.getValue<string>("slow_commit_public_key"),
      slowCommitVerificationKeys);
  auxState->commitCryptosys->loadKeys(
      config.getValue<string>("commit_public_key"), commitVerificationKeys);
  auxState->optimisticCommitCryptosys->loadKeys(
      config.getValue<string>("optimistic_commit_public_key"),
      optimisticCommitVerificationKeys);

  size_t local_node = detectLocalNode(config);
  ConcordConfiguration& localReplicaConfig =
      config.subscope("node", local_node).subscope("replica", 0);
  uint16_t localReplicaID =
      localReplicaConfig.getValue<uint16_t>("principal_id");
  if (!localReplicaConfig.hasValue<string>("slow_commit_private_key")) {
    hasRequired = false;
    parameters_missing.push_back("node[" + to_string(local_node) +
                                 "]/replica[0]/slow_commit_private_key");
    LOG4CPLUS_ERROR(logger,
                    "Configuration missing required threshold private key: "
                    "slow_commit_private_key for this node.");
  } else {
    auxState->slowCommitCryptosys->loadPrivateKey(
        (localReplicaID + 1),
        localReplicaConfig.getValue<string>("slow_commit_private_key"));
  }
  if (!localReplicaConfig.hasValue<string>("commit_private_key")) {
    hasRequired = false;
    parameters_missing.push_back("node[" + to_string(local_node) +
                                 "]/replica[0]/commit_private_key");
    LOG4CPLUS_ERROR(logger,
                    "Configuration missing required threshold private key: "
                    "commit_private_key for this node.");
  } else {
    auxState->commitCryptosys->loadPrivateKey(
        (localReplicaID + 1),
        localReplicaConfig.getValue<string>("commit_private_key"));
  }
  if (!localReplicaConfig.hasValue<string>("optimistic_commit_private_key")) {
    hasRequired = false;
    parameters_missing.push_back("node[" + to_string(local_node) +
                                 "]/replica[0]/optimistic_commit_private_key");
    LOG4CPLUS_ERROR(logger,
                    "Configuration missing required threshold private key: "
                    "optimistic_commit_private_key for this node.");
  } else {
    auxState->optimisticCommitCryptosys->loadPrivateKey(
        (localReplicaID + 1),
        localReplicaConfig.getValue<string>("optimistic_commit_private_key"));
  }
  if (!hasRequired) {
    throw ConfigurationResourceNotFoundException(
        getErrorMessageListingParameters(
            "Cannot load SBFT Cryptosystems: configuration is missing value(s) "
            "for required parameter(s): ",
            parameters_missing));
  }
}

// Note the current implementaion of this function assumes all Concord-BFT
// principal IDs in the configuration are parameters with the name
// "principal_id".
void outputPrincipalLocationsMappingJSON(ConcordConfiguration& config,
                                         ostream& output) {
  json principal_map;

  if (config.containsScope("node") && config.scopeIsInstantiated("node")) {
    for (size_t i = 0; i < config.scopeSize("node"); ++i) {
      string node_id = to_string(i + 1);
      ConcordConfiguration& node = config.subscope("node", i);

      principal_map[node_id] = json::array();
      for (auto iter =
               node.begin(ConcordConfiguration::kIterateAllInstanceParameters);
           iter !=
           node.end(ConcordConfiguration::kIterateAllInstanceParameters);
           ++iter) {
        const ConfigurationPath& path = *iter;
        if ((path.getLeaf().name == "principal_id") &&
            node.hasValue<uint16_t>(path)) {
          principal_map[node_id].emplace_back(node.getValue<uint16_t>(path));
        }
      }
    }
  }

  output << principal_map;
}

}  // namespace config
}  // namespace concord
