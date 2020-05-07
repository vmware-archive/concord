// Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
// SPDX-License-Identifier: Apache-2.0
//
// Definitions used by configuration management system.
// The most central component of this system is the ConcordConfiguration class,
// which provides a framework for defining and managing configuration
// information. Also included in this file are:
// - ConfigurationPath: a struct for specifying paths to specific configuration
// parameters within a ConcordConfiguration, accounting for the fact that
// parameters may belong to arbitrarilly nested instantiable "scopes" such as
// per-node or per-replica parameters.
// - ParameterSelection: a utility class provided to facilitate turning an
// arbitrary function for picking ConfigurationParameters into an iterable set.
// - YAMLConfigurationInput and YAMLConfigurationOutput: Provide functionality
// to input/output the contents of a ConcordConfiguration in YAML.
// - specifyConfiguration and related functions: specifyConfiguration fills out
// a ConcordConfiguration object with the configuration we are currently using;
// it is intended as the authoritative source of truth on what Concord's
// configuration currently looks like and the place to begin if you need to add
// to or modify Concord's configuration. In addition to specifyConfiguration,
// several more functions are declared (following the declaration of
// specifyConfiguration) which encode knwoledge of the current Concord
// configuration and its properties; these functions are intended to make code
// dependent on the configuration more robust and less fragile by pulling out
// logic that requires knowledge about the current configuration and collecting
// it in one place.

#ifndef CONFIG_CONFIGURATION_MANAGER_HPP
#define CONFIG_CONFIGURATION_MANAGER_HPP

#include "stdint.h"

#include <fstream>
#include <iostream>
#include <map>
#include <unordered_set>
#include <utility>

#include <cryptopp/dll.h>
#include <log4cplus/configurator.h>
#include <log4cplus/loggingmacros.h>
#include <boost/program_options.hpp>
#include <csignal>
#include <fstream>
#include <iostream>

#include "yaml-cpp/yaml.h"

#include "ThresholdSignaturesTypes.h"

namespace concord {
namespace config {

// Exception type for any exceptions thrown by the configuration system that do
// not fit into standard exception types such as std::invalid_argument. More
// specific types of configuration-specific exceptions should be subtypes of
// this exception class and, generally configuration system implementations
// should strongly prefer throwing exceptions of those subtypes of this class
// over constructing exceptions of this class directly.
class ConfigurationException : public std::exception {
 public:
  explicit ConfigurationException(const std::string& what) : message(what) {}
  virtual const char* what() const noexcept override { return message.c_str(); }

 private:
  std::string message;
};

// Exception type possibly thrown by iterators over configuration structures in
// the event an iterator has become invalid. The primary cause of such an
// iterator invalidation is modification of the underlying object that the
// iterator does not handle.
class InvalidIteratorException : public ConfigurationException {
 public:
  explicit InvalidIteratorException(const std::string& what)
      : ConfigurationException(what), message(what) {}
  virtual const char* what() const noexcept override { return message.c_str(); }

 private:
  std::string message;
};

// Exception type thrown by ConcordConfiguration on attempts to make definitions
// within the configuration it manages that conflict with existing definitions,
// for example, declaration of a configuration parameter with a name that is
// already in use.
class ConfigurationRedefinitionException : public ConfigurationException {
 public:
  explicit ConfigurationRedefinitionException(const std::string& what)
      : ConfigurationException(what), message(what) {}
  virtual const char* what() const noexcept override { return message.c_str(); }

 private:
  std::string message;
};

// Exception type thrown by ConcordConfiguration when a request requires or
// references something that does not exist or cannot be found. For example,
// this includes attempts to read the value for a configuration parameter that
// does not currently have a value loaded.
class ConfigurationResourceNotFoundException : public ConfigurationException {
 public:
  explicit ConfigurationResourceNotFoundException(const std::string& what)
      : ConfigurationException(what), message(what) {}
  virtual const char* what() const noexcept override { return message.c_str(); }

 private:
  std::string message;
};

// struct for referring to specific parameters or scopes within a
// ConcordConfiguration. ConcordConfiguration provides for the definition of
// instantiable scopes that may contain configuration parameters or other scopes
// (for example, these scopes can be used to handle per-node or per-replica
// parameters). This struct enables constructing references to parameters (or
// scopes) given that they may be contained in (possibly nested) scopes, and
// that the scopes in which they are contained may be either instances of scopes
// or the templates from which those instances are created. Several utility
// functions for workign with ConfigurationPaths are also defined for this
// struct.
// For example, a ConfigurationPath could refer to the parameter "port" for the
// 2nd replica. Such a path would be constructed from scratch like this:
//
// ConfigurationPath path("replica", (size_t)2);
// path.subpath.reset(new ConfigurationPath("port", false));
//
// This struct provides no synchronization or guarantees of synchronization.
// Behavior of this class is currently undefined in the event the subpath
// pointers are used to create a cycle.
struct ConfigurationPath {
  // Whether this segment of the path is a scope (true indicates a scope, false
  // indicates a parameter).
  bool isScope;

  // If this segment of the path is a scope, whether it is the temmplate for the
  // scope or an instance of the scope (true indicates an intance, false
  // indicates the template; note useInstance should be ignored if (isScope ==
  // false).
  bool useInstance;

  // Name for this segment of the configuration path, either the name of a scope
  // or of a parameter depending on isScope.
  std::string name;

  // If this segment of the path selects a scope instance, index selects which
  // one (note scope indexes are 0-indexed). This field should not be given any
  // meaning unless (isScope && useInstance).
  size_t index;

  // If this segment of the path is not the final segment, subpath points to the
  // rest of the path. If this is the final segment, subpath is null. Note
  // subpath should be ignored if (isScope == false). Note this
  // ConfigurationPath object is considered to own the one pointed to by
  // subpath; that one will be deleted by this path's destructor.
  std::unique_ptr<ConfigurationPath> subpath;

  // Default constructor for ConfigurationPath. This constructor is defined
  // primarily to allow declaring a ConfigurationPath by value without
  // initializing it. Although this constructor will make a path to a variable
  // with the empty string as its name, client code should not rely on the
  // behavior of this constructor.
  ConfigurationPath();

  // Constructs a ConfigurationPath with the given values for name and isScope.
  // If isScope is true, subpath will be initialized to null.
  ConfigurationPath(const std::string& name, bool isScope = false);

  // Constructs a ConfigurationPath to a scope instance with the given name and
  // index (this constructor sets isScope and useInstance to true). subpath will
  // be initialized to null.
  ConfigurationPath(const std::string& name, size_t index);

  // Copy constructor for ConfigurationPath. Note that, since each non-terminal
  // path step is considered to own the next one, this constructor will
  // recursively copy the entire path; subpath will not be alliased between
  // other and the newly constructed instance.
  ConfigurationPath(const ConfigurationPath& other);

  // Destructor for ConcigurationPath. Note subpath will be freed (and this
  // freeing will be recursive if the subpath has multiple segments).
  ~ConfigurationPath();

  // Copy assignment operator for ConfigurationPaths. this will be equal to
  // other after this operator returns. Note any subpath the caller currently
  // has will be freed, and other's subpath (if any) will be recursively copied.
  ConfigurationPath& operator=(const ConfigurationPath& other);

  // Equality and inequality operators for ConfigurationPaths. If isScope is
  // true and subpath exists, the equality check is recursive; it will not
  // merely be a test for alliasing of subpath. Note useInstance will be ignored
  // if isScope is not true, index will be ignored if isScope and useInstance
  // are not both true, and subpath will be ignored if isScope is not true.
  bool operator==(const ConfigurationPath& other) const;
  bool operator!=(const ConfigurationPath& other) const;

  // Generates a string representation of this configuration path. The primary
  // intended use case of this function is in generation of human-readable
  // output such as error messages. The format is:
  //   name
  //     for parameters or scope templates
  //   name[index]
  //     for scope instances
  // If subpath is non-null,
  //   /<STRING REPRESENTATION OF THE SUBPATH>
  // will be appended. For example, the string representation of a path to the
  // "port" parameter for the second replica would be: replica[2]/port
  std::string toString() const;

  // Returns true if and only if (1) this (the caller) is ultimately a path to a
  // scope (i.e. isScope is true for the terminal segment of this
  // ConfigurationPath) and (2) other is within that scope. Note this is
  // effectively equivalent to this being a scope and a prefix of other.
  bool contains(const ConfigurationPath& other) const;

  // Concatenate other to the end of this ConfigurationPath.
  // If this ConfigurationPath ultimately refers to a parameter, this operation
  // is illegal and will throw std::invalid_argument.
  ConfigurationPath concatenate(const ConfigurationPath& other) const;

  // Get the "leaf" of this ConfigurationPath, that is, its terminal segment.
  ConfigurationPath getLeaf() const;

  // Get a ConfigurationPath that is equivalent to this path with the "leaf"
  // (that is, its terminal segment) truncated. This will return an unaltered
  // copy of this path if trimLeaf is called directly on a single-segment path.
  ConfigurationPath trimLeaf() const;
};

// Struct for storing "auxiliary state" to a ConcordConfiguration. This class is
// intended to facilitate giving ownership of objects related to or used by the
// configuration to a ConcordConfiguration without breaking
// ConcordConfiguration's properties of being largely agnostic to the contents
// and purpose of a configuration it stores. A ConcordConfiguration can be given
// ownership of a ConfigurationAuxiliaryState with the setAuxiliaryState
// function; that ConfigurationAuxiliaryState object can then be accessed with
// the getAuxiliaryState function. The ConcordConfiguration object will destruct
// its auxiliary state object whenever the ConcordConfiguration is destroyed,
// cleared, or when setAuxiliaryState is called again.
//
// It is anticipated a typical use of auxiliary configuration will involve
// implementing a struct extending ConfigurationAuxiliaryState with member data
// whose ownership is to be given to a ConcordConfiguration and making dynamic
// casts to this subtype when the state is accessed with getAuxiliaryState.
//
// An example of a reason for using this class might be if we are storing
// cryptographic keys in a ConcordConfiguration and need to give the
// ConcordConfiguration ownership of a Cryptosystem object that is needed in
// validating and generating the keys; giving the ConcordConfiguration ownership
// of the object can help ensure the configuration does not outlive the
// cryptographic object and start generating errors or other undesirable
// behavior when trying to validate cryptographic parameters whose validator
// functions depend on the object.
struct ConfigurationAuxiliaryState {
 public:
  virtual ~ConfigurationAuxiliaryState() {}

  // When a ConcordConfiguration is copied, it will call this function to try to
  // copy the ConfigurationAuxiliaryState it owns (if it owns any auxiliary
  // state). Therefore, a complete implementation of this function for a struct
  // extending ConfigurationAuxiliaryState should return a pointer to a newly
  // allocated copy of the caller. Keep in mind that ConcordConfiguration scope
  // instantiation makes a copy of the scope template for each scope instance
  // created.
  //
  // ConcordConfiguration provides the following guarantees about how it uses
  // ConfigurationAuxiliaryState::clone:
  // - When a ConcordConfiguration is copied (via either copy construction or
  // copy assignment), if the ConcordConfiguraiton copied from owns a non-null
  // ConfigurationAuxiliaryState, then clone will be called for that state, the
  // configuration copied from will retain its auxiliary state pointer, and the
  // ConcordConfiguration copied to will be given the pointer returned by this
  // clone function.
  // - If a ConcordConfiguration is copied but the configuration copied from has
  // a null auxiliary state, no ConfigurationAuxiliaryState::clone will be
  // attempted and both the configurations copied from and to will have null
  // auxiliary state pointers.
  // - A ConcordConfiguration will never call ConfigurationAuxiliaryState::clone
  // if that configuration is never copied.
  virtual ConfigurationAuxiliaryState* clone() = 0;
};

// The ConcordConfiguration class provides a framework for defining the
// structure and contents of configuration and handles storing and managing that
// configuration. ConcordConfiguration assumes the fundamental unit of
// configuration is a configuration parameter with value representable as a
// string. In addition to allowing definition of configuration parameters,
// ConcordConfiguration also permits the definition of instantiable "scopes"
// which may contain parameters or other scopes (i.e. scopes can be nested,
// theoretically to an arbitrary depth). These scopes can be used to model
// things such as per-replica or per-node parameters. Declaring a scope creates
// a template for its instances; at a later time, the scope can be instantiated
// to create a copy of it for each instance. Scope instances and templates are
// themselves fully-featured ConcordConfiguration objects. One goal in designing
// this class is to enable a common definition of the Concord configuration to
// be shared between each agent involved in the configuration's lifecycle. At
// the time of this writing, that includes a configuration generation utility or
// service and the node consuming the configuration. Other than definition of
// parameters and instantiable scopes, this class currently supports the
// following features:
// - Iteration: The entire contents of a ConcordConfiguration, including all
// parameters, all scopes, or both, can be iterated through as a series of
// ConfigurationPaths.
// - Parameter Tagging: Any parameter can be tagged with any set of arbitrary
// string tags. This is intended to facilitate categorizing parameters.
// - Parameter Validation: Any number of validation functions may be associated
// with any parameter. The validators will be enforced when trying to load a
// parameter, causing a failure if any validator explicitly rejects the
// parameter. Validators may also be re-run at an arbitrary time for any
// parameter to enable support for validators that are sensitive to other
// parameters.
// - Default Values: A default value can be given for any parameter.
// - Parameter Generation: A generation function may be defined for any
// parameter that is automatically generated rather than taken as input.
// - Parameter Type Interpretation: Although this class internally handles and
// processes parameter values as strings, it supports interpreting them as other
// types on access via the hasValue and getValue functions (see comments for
// these functions below). This class relies on template specializations for
// type conversions, so interpretations as arbitrary types are not supported. At
// the type of this writing, conversion to the following types is supported:
//   - int
//   - short
//   - std::string
//   - uint16_t
//   - uint32_t
//   - uint64_t
//   - bool
//
// This class currently provides no synchronization or guarantees of
// synchronization and has not been designed with multiple threads writing to
// configuration concurrently in mind.
class ConcordConfiguration {
 public:
  // Return type for the three function type definitions immediately following.
  enum class ParameterStatus { VALID, INSUFFICIENT_INFORMATION, INVALID };

  // Type for parameter validator functions.
  // A validator should return VALID if it finds the parameter's value valid,
  // INVALID if it confirms the value is definitevely invalid, and
  // INSUFFICIENT_INFORMATION if it lacks the information to definitively
  // confirm the parameter's validity. When a ConcordConfiguration calls a
  // parameter validator, it gives the following arguments:
  //   - value: The value to assess the validity of.
  //   - config: The ConcordConfiguration calling this function; note validator
  //   functions will always be given a reference to the root config for this
  //   parameter (as opposed to the ConcordConfiguration representing the scope
  //   in which the parameter being validated exists).
  //   - path: The path to the parameter within this configuration for which
  //   this value is to be validated.
  //   - failureMessage: If the validator does not find value to be valid, it
  //   may choose to write back a message to failureMessage reporting the reason
  //   it did not find value to be valid.
  //   - state: An arbitrary pointer provided at the time this validator was
  //   added to the configuration. It is anticipated this will be used if this
  //   validator requires additional state.
  typedef ParameterStatus (*Validator)(const std::string& value,
                                       const ConcordConfiguration& config,
                                       const ConfigurationPath& path,
                                       std::string* failureMessage,
                                       void* state);

  // Type for parameter generation functions.
  // A parameter generator should return VALID if it was able to successfully
  // generate a valid value, INSUFFICIENT_INFORMATION if it information needed
  // by the generator (such as other parameters) is missing, and INVALID if it
  // is not possible to generate a valid value under the current state or the
  // generator otherwise fails. When a ConcordConfiguration calls a parameter
  // generator, it gives the following arguments:
  //   - config: The ConcordConfiguration calling this function. Note parameter
  //   generators will always be given a reference to the root config for this
  //   parameter (as opposed to the ConcordConfiguration representing the scope
  //   in which the parameter being generated exists).
  //   - path: Path to the parameter for which a value should be generated.
  //   - output: String pointer to output the generated value to if generation
  //   is successful (Note any value written to this pointer will be ignored by
  //   the ConcordConfiguration if the generator does not return VALID).
  //   - state: An arbitrary pointer provided at the time this generator was
  //   added to the configuration. It is anticipated this will be used if the
  //   generator requires additional state.
  typedef ParameterStatus (*Generator)(const ConcordConfiguration& config,
                                       const ConfigurationPath& path,
                                       std::string* output, void* state);

  // Type for a scope sizer function. A scope sizer function is called by the
  // ConcordConfiguration when instantiation of a scope is requested to
  // determine the appropriate size for it under the current configuration. A
  // scope sizer should return VALID if it was able to successfully determine
  // the correct size for the requested scope, INSUFFICIENT_INFORMATION if needs
  // information that is currently not available, and INVALID if it is not
  // possible to get a valid size for this scope under the current state or if
  // the sizer otherwise fails. When a ConcordConfiguration calls a scope sizer,
  // it gives the following arguments:
  //   - config: The ConcordConfiguration calling this function. Note scope
  //   sizer functioons will always be called with a reference to the root
  //   config for this parameter, even if the scope being instantiated itself
  //   exists in a subscope of the configuration.
  //   - path: Path to the scope for which a size is being requested.
  //   - output: size_t pointer to which to output the appropriate size for
  //   the requested scope (Note the ConcordConfiguration will ignore any value
  //   written here if the sizer does not return VALID).
  //   - state: An arbitrary pointer provided at the time the scope being sized
  //   was declared. It is anticipated this pointer will be used if the scope
  //   sizer requires any additional state.
  typedef ParameterStatus (*ScopeSizer)(const ConcordConfiguration& config,
                                        const ConfigurationPath& path,
                                        size_t* output, void* state);

 private:
  struct ConfigurationScope {
    std::unique_ptr<ConcordConfiguration> instanceTemplate;

    bool instantiated;
    std::vector<ConcordConfiguration> instances;
    std::string description;
    ScopeSizer size;
    void* sizerState;

    ConfigurationScope() {}
    ConfigurationScope(const ConfigurationScope& original);
    ConfigurationScope& operator=(const ConfigurationScope& original);
  };
  struct ConfigurationParameter {
    std::string description;
    bool hasDefaultValue;
    std::string defaultValue;
    bool initialized;
    std::string value;
    std::unordered_set<std::string> tags;
    Validator validator;
    void* validatorState;
    Generator generator;
    void* generatorState;

    ConfigurationParameter() {}
    ConfigurationParameter(const ConfigurationParameter& original);
    ConfigurationParameter& operator=(const ConfigurationParameter& original);
  };

  std::unique_ptr<ConfigurationAuxiliaryState> auxiliaryState;
  std::string configurationState;

  // Both these pointers point to null if this ConcordConfiguration is the root
  // scope of its configuration.
  ConcordConfiguration* parentScope;
  std::unique_ptr<ConfigurationPath> scopePath;

  std::map<std::string, ConfigurationScope> scopes;
  std::map<std::string, ConfigurationParameter> parameters;

  // Private helper functions.
  void invalidateIterators();
  ConfigurationPath* getCompletePath(const ConfigurationPath& localPath) const;
  std::string printCompletePath(const ConfigurationPath& localPath) const;
  std::string printCompletePath(const std::string& localParameter) const;
  void updateSubscopePaths();
  ConfigurationParameter& getParameter(const std::string& parameter,
                                       const std::string& failureMessage);
  const ConfigurationParameter& getParameter(
      const std::string& parameter, const std::string& failureMessage) const;
  const ConcordConfiguration* getRootConfig() const;

  template <typename T>
  bool interpretAs(std::string value, T& output) const;
  template <typename T>
  std::string getTypeName() const;

 public:
  // Complete definition of the iterator type for iterating through a
  // ConcordConfiguration returned by begin and end.
  class ConfigurationIterator : public std::iterator<std::forward_iterator_tag,
                                                     const ConfigurationPath> {
   private:
    bool recursive;
    bool scopes;
    bool parameters;
    bool instances;
    bool templates;

    ConcordConfiguration* config;

    // Value returned by this iterator; the iterator stores this value itself
    // because it returns values by reference rather than by value.
    ConfigurationPath retVal;

    // State of iteration.
    std::map<std::string, ConfigurationScope>::iterator currentScope;
    std::map<std::string, ConfigurationScope>::iterator endScopes;
    bool usingInstance;
    size_t instance;
    std::unique_ptr<ConfigurationIterator> currentScopeContents;
    std::unique_ptr<ConfigurationIterator> endCurrentScope;
    std::map<std::string, ConfigurationParameter>::iterator currentParam;
    std::map<std::string, ConfigurationParameter>::iterator endParams;
    bool invalid;

    void updateRetVal();

   public:
    ConfigurationIterator();
    ConfigurationIterator(ConcordConfiguration& configuration,
                          bool recursive = true, bool scopes = true,
                          bool parameters = true, bool instances = true,
                          bool templates = true, bool end = false);
    ConfigurationIterator(const ConfigurationIterator& original);
    ~ConfigurationIterator();

    ConfigurationIterator& operator=(const ConfigurationIterator& original);
    bool operator==(const ConfigurationIterator& other) const;
    bool operator!=(const ConfigurationIterator& other) const;
    const ConfigurationPath& operator*() const;
    ConfigurationIterator& operator++();
    ConfigurationIterator operator++(int);

    void invalidate();
  };

 private:
  // Iterators that need to be invalidated in the event this
  // ConcordConfiguration is modified.
  std::unordered_set<ConfigurationIterator*> iterators;

  void registerIterator(ConfigurationIterator* iterator);
  void deregisterIterator(ConfigurationIterator* iterator);

 public:
  // Type for iterators over this ConcordConfiguration returned by the begin and
  // end functions below. These iterators return a series of const
  // ConfigurationPath references pointing to each of the objects (scopes or
  // parameters) in the configuration that the iterator iterates through.
  // Iterators are forward iterators and have all behvior that is standard of
  // such iterators in C++. This includes:
  //   - Support for copy construction and copy assignment.
  //   - Support for operators == and != to check if two iterators are currently
  //   at the same position.
  //   - Support for operator * to get the value at the iterator's current
  //   position.
  //   - Support for prefix and postfix operator ++ to advance the iterator.
  // Note ConcordConfiguration::Iterators currently do not guarantee that the
  // ConfigurationPath references they return will still refer to the same
  // ConfigurationPath as they returned once the iterator has been advanced past
  // the point where the reference was obtained; code using
  // ConcordConfiguration::Iterators should make its own copy of the value
  // stored by the reference the iterator returns if it needs the value past
  // advancing the iterator.
  typedef ConfigurationIterator Iterator;

  ConcordConfiguration();
  ConcordConfiguration(const ConcordConfiguration& original);
  ~ConcordConfiguration();
  ConcordConfiguration& operator=(const ConcordConfiguration& original);

  // Removes all parameter definitions and scope definitions for this
  // ConcordConfiguration and resets its "configuration state" to the empty
  // string.
  void clear();

  // Gives this ConcordConfiguration ownership of a ConfigurationAuxiliaryState
  // object. The ConfigurationAuxiliaryState object will be freed when this
  // function is called again, when this ConcordConfiguration is cleared, or
  // when this ConcordConfiguration is destructed, but not before then.
  void setAuxiliaryState(ConfigurationAuxiliaryState* auxState);

  // Accesses and returns a pointer to the ConfigurationAuxiliaryState object
  // this ConcordConfiguration has been given ownership of, if any. Returns
  // nullptr if this ConcordConfiguration has not been given any auxiliary
  // state.
  ConfigurationAuxiliaryState* getAuxiliaryState();
  const ConfigurationAuxiliaryState* getAuxiliaryState() const;

  // Sets a "configuration state label" for this configuration to the given
  // string. ConcordConfiguration itself does not use its own "configuration
  // state label", but it can be retrieved and used at any time by client code
  // with getConfigurationStateLabel.
  void setConfigurationStateLabel(const std::string& state);

  // Gets the most recent value passed to setConfigurationStateLabel. Returns an
  // empty string if setConfigurationStateLabel has never been called for this
  // ConcordConfiguration or if clear has been called more recently than
  // setConfigurationStateLabel.
  std::string getConfigurationStateLabel() const;

  // Adds a new instantiable scope to this configuration. The scope begins empty
  // and uninstantiated.
  // Arguments:
  //   - scope:  name for the new scope being declared. A
  //   ConfigurationRedefinitionException will be thrown if a scope with this
  //   name already exists or if the name is already in use by a parameter. The
  //   empty string is disallowed as a scope name. Furthermore, scope names are
  //   disallowed from having an ending matching kYAMLScopeTemplateSuffix
  //   (defined below). This function will throw an std::invalid_argument if
  //   scope is not a valid scope name.
  //   - description: a description for this scope.
  //   - size: pointer to a scope sizer function to be used to get the
  //   appropriate size for this scope when it is instantiated. An
  //   std::invalid_argument will be thrown if a nullptr is given.
  //   - sizerState: an arbitrary pointer to be passed to the scope sizer
  //   whenever it is called. It is anticipated this pointer will be used if the
  //   sizer requires any additional state.
  void declareScope(const std::string& scope, const std::string& description,
                    ScopeSizer size, void* sizerState);

  // Gets the description the scope named by the parameter was constructed with.
  // Throws a ConfigurationResourceNotFoundException if the requested scope does
  // not exist.
  std::string getScopeDescription(const std::string& scope) const;

  // Instantiates the scope named by the parameter. The sizer function provided
  // when the scope was declared will be called to determine its size.
  // Instantiation will not be attempted unless the sizer function returns
  // VALID. If the scope is instantiated, a new copy of current scope template
  // will be made for each instance. If this scope has was already instantiated,
  // any existing instances will be deleted when this function instantiates it
  // again. Returns the ParameterStatus returned by the scope's sizer function.
  // Throws a ConfigurationResourceNotFoundException if no scope exists with the
  // given name.
  ParameterStatus instantiateScope(const std::string& scope);

  // Gets a reference to the scope template for the scope with the name
  // specified by the parameter. Modifying the ConcordConfiguration returned by
  // this function will also modify the scope it represents within the
  // ConcordConfiguration used to call this function. Throws a
  // ConfigurationResourceNotFoundException if no scope exists with the given
  // name.
  ConcordConfiguration& subscope(const std::string& scope);
  const ConcordConfiguration& subscope(const std::string& scope) const;

  // Gets a reference to a scope instance from the scope named by the scope
  // (string) parameter with index specified by the index (size_t) parameter
  // (note scope instances are 0-indexed). Modifying the ConcordConfiguration
  // returned by this function will also modify the instance within the
  // ConcordConfiguration used to call this function. Throws a
  // ConfigurationResourceNotFoundEsception if no scope exists with the
  // requested name, if the reuested scope has not been instantiated, or if the
  // requested index is out of ragne.
  ConcordConfiguration& subscope(const std::string& scope, size_t index);
  const ConcordConfiguration& subscope(const std::string& scope,
                                       size_t index) const;

  // Gets a reference to a scope template or scope instance within this
  // ConcordConfiguration (or recursively within any scope within this
  // ConcordConfiguration, where "within" is fully transitive) specified by the
  // given path. Throws a ConfigurationResourceNotFoundException if no scope
  // exists at the specified path within this ConcordConfiguration.
  ConcordConfiguration& subscope(const ConfigurationPath& path);
  const ConcordConfiguration& subscope(const ConfigurationPath& path) const;

  // Returns true if this configuration contains a scope with the given name and
  // false otherwise.
  bool containsScope(const std::string& name) const;

  // Returns true if this configuration contains a scope template or scope
  // instance at the given path and false otherwise.
  bool containsScope(const ConfigurationPath& path) const;

  // Returns true if this configuration contains a scope with the given name and
  // that scope has been instantiated and false otherwise. Note a scope may be
  // considered instantiated even if it has 0 instances in the event the scope
  // sizer returned 0 at instantiation time.
  bool scopeIsInstantiated(const std::string& name) const;

  // Returns the size (number of instances) that the scope named by the
  // parameter is currently instantiated to. Throws a
  // ConfigurationResourceNotFoundException if the requested scope does not
  // exist or has not been instantiated.
  size_t scopeSize(const std::string& scope) const;

  // Declares a new parameter in this configuration with the given name and
  // description and with no default value. Throws a
  // ConfigurationRedefinitionException if a parameter already exists with the
  // given name or if that name is already used by a scope.
  // The empty string is disallowed as a parameter name. Furthermore, parameters
  // are disallowed from having an ending matching kYAMLScopeTemplateSuffix
  // (defined below). This function will throw an std::invalid_argument if name
  // is not a valid parameter name.
  void declareParameter(const std::string& name,
                        const std::string& description);

  // Declares a new parameter in this configuration with the given name,
  // description, and default value. Throws a ConfigurationRedifinitionException
  // if a parameter already exists with the given name or if that name is
  // already used by a scope.
  // The empty string is disallowed as a parameter name. Furthermore, parameters
  // are disallowed from having an ending matching kYAMLScopeTemplateSuffix
  // (defined below). This function will throw an std::invalid_argument if name
  // is not a valid parameter name.
  void declareParameter(const std::string& name, const std::string& description,
                        const std::string& defaultValue);

  // Adds any tags in the given vector that the parameter in this configuration
  // with the given name is not already tagged with to that parameter. Throws a
  // ConfigurationResourceNotFoundException if no parameter exists  in this
  // configuration with the given name.
  void tagParameter(const std::string& name,
                    const std::vector<std::string>& tags);

  // Returns true if the parameter in this configuration with the given name
  // exists and has been tagged with the given tag, and false if it exists but
  // has not been tagged with this tag. Throws a
  // ConfigurationResourceNotFoundException if no parameter exists with this
  // name.
  bool isTagged(const std::string& name, const std::string& tag) const;

  // Gets the description that the parameter with the given name was declared
  // with. Throws a ConfigurationResourceNotFoundException if no parameter
  // exists in this ConcordConfiguration with the given name.
  std::string getDescription(const std::string& name) const;

  // Adds a validation function to a configuration parameter in this
  // ConcordConfiguration. Note that we allow only one validation function per
  // parameter. Calling this function for a parameter that already has a
  // validator will cause the existing validator to be replaced. Arguments:
  //   - name: The name of the parameter to add the validator to. Throws a
  //   ConfigurationResourceNotFoundException if no parameter exists in this
  //   ConcordConfiguration with this name.
  //   - validator: Function pointer to the validation function to add to this
  //   parameter. Throws an std::invalid_argument if this is a null pointer.
  //   - validatorState: Arbitrary pointer to pass to the validator each time it
  //   is called; it is expected this pointer will be used if the validator
  //   requires any additional state.
  void addValidator(const std::string& name, Validator validator,
                    void* validatorState);

  // Adds a generation function to a configuration parameter in this
  // ConcordConfiguration. Note that a parameter can only have a single
  // generator function; calling this function for a parameter that already has
  // a generator function will replace the existing generator if this function
  // is successful. Arguments:
  //   - name: The name of the parameter to add the generator to. Throws a
  //   ConfigurationResourceNotFoundException if no parameter exists in this
  //   ConcordConfiguration with this name.
  //   - generator: Function pointer to the generator function to be added. An
  //   std::invalid_argument will be thrown if a nullptr is given for this
  //   parameter.
  //   - generatorState: An arbitrary pointer to pass to the generator each time
  //   it is called; it is expected this pointer will be used if the generator
  //   requires additional state.
  void addGenerator(const std::string& name, Generator generator,
                    void* generatorState);

  // Returns true if this ConcordConfiguration contains a parameter with the
  // given name and false otherwise.
  bool contains(const std::string& name) const;

  // Returns true if there is a parameter within this ConcordConfiguraiton at
  // the given path and false otherwise. Note this function handles checking the
  // appropriate subscope for the parameter if the path has multiple segments.
  bool contains(const ConfigurationPath& path) const;

  // Returns true if this ConcordConfiguration contains a parameter with the
  // given name, that parameter has a value loaded, and that value can be
  // validly converted to type T; returns false otherwise. Note this function
  // relies on template specialization for type conversions, so not all types
  // are supported; see the comments for class ConcordConfiguration for a list
  // loadClusterSizeParameters(yamlInput, config, &(std::cout));

  // of currently supported types.
  template <typename T>
  bool hasValue(const std::string& name) const {
    T result;
    return contains(name) && (parameters.at(name).initialized) &&
           interpretAs<T>(parameters.at(name).value, result);
  }

  // Returns true if there is a parameter within this ConcordConfiguration at
  // the given path, that parameter has a value loaded, and that value can be
  // validly converted to type T; returns false otherwise. Note this function
  // handles checking the appropriate subscope for the parameter if the path has
  // multiple segments. Note this function relies on template specialization for
  // type conversions, so not all types are supported; see the comments for
  // class ConcordConfiguration for a list of currently supported types.
  template <typename T>
  bool hasValue(const ConfigurationPath& path) const {
    if (!contains(path)) {
      return false;
    }
    const ConcordConfiguration* containingScope = this;
    if (path.isScope && path.subpath) {
      containingScope = &(subscope(path.trimLeaf()));
    }
    return (containingScope->hasValue<T>(path.getLeaf().name));
  }

  // Gets the value currently loaded to the parameter in this
  // ConcordConfiguration with the given name, interpreted as type T. Throws a
  // ConfigurationResourceNotFoundException if this ConcordConfiguration does
  // not contain a parameter with the given name, if the requested parameter
  // does not have a value loaded, or if the loaded value cannot be interpreted
  // as the requested type. Note this function relies on template specialization
  // for type conversions, so not all types are supported; see the comments for
  // class ConcordConfiguration for a list of currently supported types.
  template <typename T>
  T getValue(const std::string& name) const {
    const ConfigurationParameter& parameter =
        getParameter(name, "Could not get value for parameter ");
    if (!(parameter.initialized)) {
      throw ConfigurationResourceNotFoundException(
          "Could not get value for parameter " +
          printCompletePath(ConfigurationPath(name, false)) +
          ": parameter is uninitialized.");
    }
    T result;
    if (!interpretAs<T>(parameter.value, result)) {
      throw ConfigurationResourceNotFoundException(
          "Could not get value for parameter " +
          printCompletePath(ConfigurationPath(name, false)) +
          ": parameter value \"" + parameter.value +
          "\" could not be interpreted as a(n) " + getTypeName<T>() + ".");
    }
    return result;
  }

  // Gets the value currently loaded to the parameter in this
  // ConcordConfiguration at the given path. Throws a
  // ConfigurationResourceNotFoundException if there is no parameter at the
  // given path, if the requested parameter does not have a value loaded, or if
  // the loaded value cannot be interpreted as the requested type. Note this
  // function relies on template specialization for type conversions, so not all
  // types are supported; see the comments for class ConcordConfiguration for a
  // list of currently supported types.
  template <typename T>
  T getValue(const ConfigurationPath& path) const {
    if (!contains(path)) {
      throw ConfigurationResourceNotFoundException(
          "Could not get value for parameter " + printCompletePath(path) +
          ": parameter not found.");
    }
    const ConcordConfiguration* containingScope = this;
    if (path.isScope && path.subpath) {
      containingScope = &(subscope(path.trimLeaf()));
    }
    const ConfigurationParameter& parameter = containingScope->getParameter(
        path.getLeaf().name, "Could not get value for parameter ");
    if (!parameter.initialized) {
      throw ConfigurationResourceNotFoundException(
          "Could not get value for parameter " + printCompletePath(path) +
          ": parameter is uninitialized.");
    }
    T result;
    if (!interpretAs<T>(parameter.value, result)) {
      throw ConfigurationResourceNotFoundException(
          "Could not get value for parameter " + printCompletePath(path) +
          ": parameter value \"" + parameter.value +
          "\" could not be interpreted as a(n) " + getTypeName<T>() + ".");
    }
    return result;
  }

  // Loads a value to a parameter in this ConcordConfiguration. This function
  // will return without loading the requested value if the parameter's
  // validator (if any) returns an INVALID status for the requested value.
  // Arguments:
  //   - name: The name of the parameter to which to load the value. Throws a
  //   ConfigurationResourceNotFoundException if no parameter exists with this
  //   name.
  //   - value: Value to attempt to load to this parameter.
  //   - failureMessage: If a non-null pointer is given for failureMessage, the
  //   named parameter has a validator, and that validator returns a status
  //   other than valid for value, then any failure message the validator
  //   provides will be written back to failureMessage.
  //   - overwrite: If the requested parameter already has a value loaded,
  //   loadValue will not overwrite it unless true is given for this parameter.
  //   - prevValue: If loadValue successfully overwrites an existing value and
  //   prevValue is non-null, the existing value that was overwritten will be
  //   written to prevValue.
  // Returns: the result of running the validator (if any) for this parameter
  // for the requested value. If the parameter has no validator, VALID will be
  // returned.
  ParameterStatus loadValue(const std::string& name, const std::string& value,
                            std::string* failureMessage = nullptr,
                            bool overwrite = true,
                            std::string* prevValue = nullptr);

  // Erases the value currently loaded (if any) for a parameter with the given
  // name in this ConcordConfiguration. The parameter will have no value loaded
  // after this function runs. If a non-null pointer is provided for prevValue
  // and this function does erase a value, then the erased value will be written
  // back to prevValue. Throws a ConfigurationResourceNotFoundException if no
  // parameter exists with the given name.
  void eraseValue(const std::string& name, std::string* prevValue = nullptr);

  // Erases any and all values currently loaded in this ConcordConfiguration. No
  // parameter in this configuraiton (including in any subscope of this
  // configuration) will have a value loaded after this function runs.
  void eraseAllValues();

  // Loads the default value for a given parameter. The value will not be loaded
  // if the validator for the parameter (if any) returns an INVALID status for
  // the default value. Arguments:
  //   - name: The name of the parameter for which to load the default value.
  //   Throws a ConfigurationResourceNotFoundException if no parameter exists
  //   with this name, or if the named parameter has no default value.
  //   - failureMessage: If a non-null pointer is given for failureMessage, the
  //   named parameter has a validator, and that validator returns a status
  //   other than valid for the default value of the named parameter, then any
  //   failure message the validator provides will be written back to
  //   failureMessage.
  //   - overwrite: If the selected parameter already has a value loaded, that
  //   value will only be overwritten if true is given for the overwrite
  //   parameter.
  //   - prevValue: If a non-null pointer is given for prevValue and loadDefault
  //   does successfully overwrite an existing value, the existing value will be
  //   written back to prevValue.
  // Returns: the result of running the validator (if any) for the default value
  // for this parameter. If the parameter has no validators, VALID will be
  // returned.
  ParameterStatus loadDefault(const std::string& name,
                              std::string* failureMessage = nullptr,
                              bool overwrite = false,
                              std::string* prevValue = nullptr);

  // Load the default values for all parameters within this
  // ConcordConfiguration, including in any subscopes, that have default values.
  // Any default value for which a validator of its parameter returns INVALID
  // will not be loaded. Existing values will not be overwritten unless true is
  // given for the overwrite parameter. Scope templates will be ignored unless
  // true is given for the includeTemplates parameter. Returns: the result of
  // running the validator(s) for every default parameter considered (i.e.
  // excluding those in scope templates unless includeTemplates is specified).
  // In aggregating the results of running these validator(s), the "least valid"
  // result obtained will be returned. That is, VALID will only be returned if
  // every validator run returns VALID, and INVALID will be returned over
  // INSUFFICIENT_INFORMATION if any validator returns INVALID.
  ParameterStatus loadAllDefaults(bool overwrite = false,
                                  bool includeTemplates = false);

  // Runs the validator (if any) for the currently loaded value of the named
  // parameter and return its result. Throws a
  // ConfigurationResourceNotFoundException if no parameter exists with the
  // given name. If the requested parameter exists but has no value loaded,
  // validate will return INSUFFICIENT_INFORMATION. If the named parameter
  // exists, has a value loaded, and has no validators, VALID will be returned.
  // If a non-null pointer is given for failureMessage, the named parameter has
  // a validator, and the result is not VALID, then any failure message provided
  // by the validator will be written back to failureMessage.
  ParameterStatus validate(const std::string& name,
                           std::string* failureMessage = nullptr) const;

  // Runs the validator(s) for the currently loaded value(s) in all parameter(s)
  // in this ConcordConfiguration, including in any subscopes, and returns their
  // result. If this results in no validators being run, then VALID will be
  // returned. In aggregating the results of multiple validators, the "least
  // valid" result obtained will be returned. That is, VALID will only be
  // returned if every validator run returns VALID, and INVALID will be returned
  // over INSUFFICIENT_INFORMATION if any validator returns INVALID. If false is
  // given for the includeTemplates parameter, then scope templates will be
  // skipped. If true is given for ignoreUninitializedParameters, then any
  // parameter with no value loaded will be skipped entirely. Otherwise, the
  // result of validation for any uninitialized parameter will be considered to
  // be INSUFFICIENT_INFORMATION.
  ParameterStatus validateAll(bool ignoreUninitializedParameters = true,
                              bool inclueTemplates = false);

  // Runs a parameter's generation function and loads the result. generate will
  // not attempt to load the generated value unless the generation function
  // returns VALID. Generate also will not load any generated value for which
  // the validator of the selected parameter returns INVALID. Parameters:
  //   - name: Name of the parameter for which to generate a value. A
  //   ConfigurationResourceNotFoundException will be thrown if no parameter
  //   exists with the given name or if the named parameter has no generation
  //   funciton.
  //   - failureMessage: If a non-null pointer is given for failure message, the
  //   named parameter has both a generator and validator, and the geneerator
  //   returns the status VALID but the validator returns a non-VALID status for
  //   the generated value, then any failure message provided by the validator
  //   will be written back to failureMessage.
  //   - overwrite: If the requested parameter is already initialized, the
  //   existing value will only be overwritten if true is given for overwrite.
  //   - prevValue: If generate does successfully overwrite an existing value
  //   and a non-null pointer was given for prevValue, the existing value will
  //   be written back to prevValue.
  // Returns:
  // If the parameter's generation function returns a value other than VALID,
  // then that value will be returned; otherwise, generate returns the result of
  // running the parameter's validator (if any) on its generated values. If the
  // generator reutnred VALID but the parameter has no validators, VALID will be
  // returned.
  ParameterStatus generate(const std::string& name,
                           std::string* failureMessage = nullptr,
                           bool overwrite = false,
                           std::string* prevValue = nullptr);

  // Runs the generation functions for any and all parameters in this
  // ConcordConfiguration that have generation functions and loads the generated
  // values. A value produced by a generator will not be loaded unless the
  // generator returned VALID. Furthermore, a value will not be loaded if the
  // parameter it would be loaded to has a validator function and that validator
  // returns INVALID. generateAll will only overwrite existing values of
  // initialized parameters if true is given for overwrite. generateAll will
  // ignore scope templates if false is given for includeTemplates. Returns: The
  // "least valid" result from the generation of any parameter that is not
  // skipped (templates are skipped if false is given for includeTemplates, and
  // parameters without generation functions are always skipped). That is, VALID
  // will not be returned unless the result for every non-skipped parameter is
  // VALID, and INVALID will be returned over INSUFFICIENT_INFORMATION if
  // INVALID is obtained for any parameter. The result for each non-skipped
  // parameter individually in this aggregation process will be considered to be
  // the same as the result the generate function would return for generation of
  // just that parameter.
  ParameterStatus generateAll(bool overwrite = false,
                              bool includeTemplates = false);

  // ConcordConfiguration::begin and ConcordConfiguration::end, which both
  // should be declared immediately below these constant definitions; both
  // essentially require enough boolean parameters that having them all as
  // individual parameters in their function signatures would be somewhat
  // unsightly. To make calls to these functions more intuitive and legible, we
  // encode these five booleans in a bitmask. The following constants give the
  // bits used in this bitmasks and a number of complete bitmasks for common
  // selections of features. At the time of this comment's writing, there are
  // five distinct feature bits used in selecting the features of a
  // ConcordConfiguration::Iterator, specifically:
  // - kTraverseParameters: If this bit is set to 0, the iterator will not
  // return any paths to parameters.
  // - kTraverseScopes: If this bit is set to 0, the iterator will not return
  // any paths to scopes.
  // - kTraverseTemplates: If this bit is set to 0, the iterator will ignore
  // scope templates entirely.
  // - kTraverseInstances: If this bit is set to 0, the iterator will ignore
  // scope instances entirely.
  // - kTraverseRecursively: If this bit is set to 0, the iterator will not
  // recursively traverse subscopes of the ConcordConfiguration this iterator is
  // constructed for. That is, it may return parameters directly in this
  // configuration objectand single-step paths to scope templates and/or
  // instances that lie directly within this configuration object, but it will
  // not return any paths with more than one step which would require entering
  // subscopes.
  typedef uint8_t IteratorFeatureSelection;

  const static IteratorFeatureSelection kTraverseParameters = (0x01 << 0);
  const static IteratorFeatureSelection kTraverseScopes = (0x01 << 1);
  const static IteratorFeatureSelection kTraverseTemplates = (0x01 << 2);
  const static IteratorFeatureSelection kTraverseInstances = (0x01 << 3);
  const static IteratorFeatureSelection kTraverseRecursively = (0x01 << 4);

  const static IteratorFeatureSelection kIterateTopLevelParameters =
      kTraverseParameters;
  const static IteratorFeatureSelection kIterateTopLevelScopeTemplates =
      kTraverseScopes | kTraverseTemplates;
  const static IteratorFeatureSelection kIterateTopLevelScopeInstances =
      kTraverseScopes | kTraverseInstances;
  const static IteratorFeatureSelection kIterateAllTemplateParameters =
      kTraverseParameters | kTraverseTemplates | kTraverseRecursively;
  const static IteratorFeatureSelection kIterateAllInstanceParameters =
      kTraverseParameters | kTraverseInstances | kTraverseRecursively;
  const static IteratorFeatureSelection kIterateAllParameters =
      kTraverseParameters | kTraverseTemplates | kTraverseInstances |
      kTraverseRecursively;
  const static IteratorFeatureSelection kIterateAll =
      kTraverseParameters | kTraverseScopes | kTraverseTemplates |
      kTraverseInstances | kTraverseRecursively;

  // Obtains an iterator to the beginning of this ConcordConfiguration. The
  // iterator returns the selected contents of this configuration as a series of
  // const ConfigurationPath references.
  // begin accepts one parameter, features, which is a bitmask for feature
  // selection. Definition and documentation of constants for the bits in this
  // bitmask and some common feature selections combining them should be
  // immediately above this declaration. Note ConcordConfiguration::Iterators
  // currently do not guarantee that the ConfigurationPath references they
  // return will still refer to the same ConfigurationPath as they returned once
  // the iterator has been advanced past the point where the reference was
  // obtained; code using ConcordConfiguration::Iterators should make its own
  // copy of the value stored by the reference the iterator returns if it needs
  // the value past advancing the iterator.
  Iterator begin(
      IteratorFeatureSelection features = kIterateAllInstanceParameters);

  // Obtains an iterator to the end of this ConcordConfiguration. The iterator
  // will match the state of an iterator obtained with the begin function
  // immediately above, given the same feature selection bitmask, once that
  // iterator has exhausted all its contents.
  Iterator end(
      IteratorFeatureSelection features = kIterateAllInstanceParameters);
};

// A ParameterSelection object is intended to wrap a function that picks
// parameters from a configuration (by giving a boolean verdict on whether any
// particular parameter is contained in the set) and make it an iterable
// collection (with iteration returning ConfigurationPaths for each parameter
// that the function approves of). This class is intended to facilitate jobs
// that should be performed for some arbitrary subset of the configuration
// parameters, such as as I/O on certain parameters.
//
// This class currently does not provide any synchronization or guarantees of
// synchronization.
class ParameterSelection {
 public:
  // Type for parameter selector functions that this ParameterSelection wraps. a
  // ParameterSelector is to accept the following parameters:
  //   - config: The ConcordConfiguration to which the parameter assessed
  //   belongs.
  //   - path: Path to a parameter within the provided conviguration to be
  //   evaluated. The ParameterSelector should return true if the specified
  //   parameter is within this selection and false otherwise.
  //   - state: An arbitrary pointer provided at the time the ParameterSelection
  //   was constructed with this ParameterSelector. It is anticipated this
  //   pointer will be used if the ParameterSelector requires any additional
  //   state.
  typedef bool (*ParameterSelector)(const ConcordConfiguration& config,
                                    const ConfigurationPath& path, void* state);

 private:
  class ParameterSelectionIterator
      : public std::iterator<std::forward_iterator_tag,
                             const ConfigurationPath> {
   private:
    ParameterSelection* selection;
    ConcordConfiguration::Iterator unfilteredIterator;
    ConcordConfiguration::Iterator endUnfilteredIterator;
    bool invalid;

   public:
    ParameterSelectionIterator();
    ParameterSelectionIterator(ParameterSelection* selection, bool end = false);
    ParameterSelectionIterator(const ParameterSelectionIterator& original);
    virtual ~ParameterSelectionIterator();

    ParameterSelectionIterator& operator=(
        const ParameterSelectionIterator& original);
    bool operator==(const ParameterSelectionIterator& other) const;
    bool operator!=(const ParameterSelectionIterator& other) const;
    const ConfigurationPath& operator*() const;
    ParameterSelectionIterator& operator++();
    ParameterSelectionIterator operator++(int);

    void invalidate();
  };

  ConcordConfiguration* config;
  ParameterSelector selector;
  void* selectorState;

  // Set for keeping track of any iterators over this ParameterSelection so
  // they can be invalidated in the event of a concurrent modification of this
  // ParameterSelection. At the time of this writing, the only
  // ParameterSelection function which is actually considered to modify it and
  // invalidate its iterators is its destructor.
  std::unordered_set<ParameterSelectionIterator*> iterators;

  // Private helper functions.
  void registerIterator(ParameterSelectionIterator* iterator);
  void deregisterIterator(ParameterSelectionIterator* iterator);
  void invalidateIterators();

 public:
  // Type for iterators through this ParameterSelection returned by the begin
  // and end functions below. These iterators return a series of const
  // ConfigurationPath references pointing to each parameter in the selction.
  // Iterators are forward iterators and have all behvior that is standard of
  // such iterators in C++. This includes:
  //   - Support for copy construction and copy assignment.
  //   - Support for operators == and != to check if two iterators are currently
  //   at the same position.
  //   - Support for operator * to get the value at the iterator's current
  //   position.
  //   - Support for prefix and postfix operator ++ to advance the iterator.
  typedef ParameterSelectionIterator Iterator;

  // Primary constructor for ParameterSelections.
  // Parameters:
  //   - config: ConcordConfiguration that this ParameterSelection should select
  //   its parameters from.
  //   - selector: Function pointer of the ParameterSelector type defined above
  //   that decides whether given parameters are within this selection. This
  //   constructor will throw an std::invalid_argument if selector is a null
  //   pointer.
  //   - selectorState: Arbitrary pointer to be passed to selector each time it
  //   is called. It is anticipated this pointer will be used for any
  //   additioinal state the selector requires.
  ParameterSelection(ConcordConfiguration& config, ParameterSelector selector,
                     void* selectorState);

  ParameterSelection(const ParameterSelection& original);
  ~ParameterSelection();

  bool contains(const ConfigurationPath& parameter) const;
  ParameterSelection::Iterator begin();
  ParameterSelection::Iterator end();
};

// A suffix appended to scope names by YAMLConfigurationInput and
// YAMLConfigurationOutput to denote values for scope templates in the
// configuration files. In our current implementations, it is useful to have
// separate identifiers for scope templates and scope instances because we
// generally model the contents of a scope as a list of its instances and there
// is not a particularly natural way to include the contents of the template in
// the instance list.
const std::string kYAMLScopeTemplateSuffix = "__TEMPLATE";

// This class handles reading and inputting ConcordConfiguration values
// serialized to a YAML file. It is intended to be capable of reading
// configuration files output with the YAMLConfigurationOutput class below. With
// respect to configuration file format, this class expects a
// ConcordConfiguration will be serialized in YAML as follows:
// - A ConcordConfiguration, either the primary/root configuration or any of its
// subscope instances and/or templates, is represented with a YAML map.
// - Parameters in a particular scope are serialized in the map representing
// that ConcordConfiguration as an assignment of the parameter's value to the
// parameter's name.
// - A scope templates serialized in this configuration file is represented as a
// map (containing the template's contents) assiggned as a value to a key equal
// to the concatenation of the scope's name and kYAMLScopeTemplateSuffix.
// - Any instantiated scope instnaces serialized in this configuration file
// should be represented by a list of maps, where each map represents the
// contents of an instance. Each map entry in this list represents the scope
// instance with the same index (note scope instances are 0-indexed). This list
// should itself be mapped as a value to a key equal to the name of the
// instantiated scope, and this assigment should be in the map for the
// ConcordConfiguration containing this instantiated scope.
//
// This class currently does not provide any synchronization or guarantees of
// synchronization.
class YAMLConfigurationInput {
 private:
  std::istream* input;
  YAML::Node yaml;
  bool success;

  void loadParameter(ConcordConfiguration& config,
                     const ConfigurationPath& path, const YAML::Node& obj,
                     log4cplus::Logger* errorOut, bool overwrite);

 public:
  // Constructor for YAMLConfigurationInput; it accepts an std::istream. It is
  // expected that the code using the YAMLConfiguration will call parseInput to
  // have this YAMLConfigurationInput parse the input from that stream.
  YAMLConfigurationInput(std::istream& input);

  ~YAMLConfigurationInput();

  // Parses the input from the std::istream given to this
  // YAMLConfigurationInputas YAML and records it for later retrieval with
  // loadConfiguration.
  //
  // Throws: Any exception that occurs while attempting to parse this input. It
  // is anticipated this may include I/O exceptions and YAML parse failures.
  void parseInput();

  // Loads values from the configuration file this YAMLConfigurationInput parsed
  // to the given ConcordConfiguration. Parameters:
  // - config: ConcordConfiguration to load the requested values to, if
  // possible.
  // - iterator: Iterator returning ConfigurationPaths or ConfigurationPath
  // references of paths to parameters within config to attempt to load values
  // for.
  // - end: Iterator to the end of the range of ConfigurationPaths requested,
  // used to tell when iterator has finished iterating.
  // - errorOut: If a non-null logger pointer is provided for errorOut, then an
  // error message will be output to that logger in any case where
  // loadConfiguration tries to load a value from its input to config, but
  // config rejects that value as invalid. These error messages will be of the
  // format: "Cannot load value for parameter <PARAMETER NAME>: <FAILURE
  // MESSAGE>".
  // - overwrite: loadConfiguration will only overwrite existing values in
  // config if true is given for the overwrite parameter.
  // Returns: true if this YAMLConfigurationInput was able to successfully parse
  // the specified YAML configuration file and false otherwise. Note
  //  loadClusterSizeParameters(yamlInput, config, &(std::cout));
  // YAMLConfigurationInput only considers failures to consist of either (a)
  // parseInput was never called for this YAMLConfiguraitonInput or (b)
  // parseInput was exited before it returned normally due to an exception
  // occuring while trying to parse its input; it does not consider it a failure
  // if the YAML parse contains unexpected extra information or if the parse is
  // missing requested parameter(s). If false is returned, this function will
  // not have loaded any values to config. If true is returned, this function
  // will have loaded values from the configuration file it parsed to config for
  // any parameter that meets the following requirements:
  // - iterator returned a path to this parameter.
  // - This parameter exists (was declared) in config.
  // - The input configuration file had a value for this parameter.
  // - config has no value already loaded for this parameter or overwrite is
  // true.
  // - config did not reject the loaded value of the parameter due to validator
  // rejection.
  template <class Iterator>
  bool loadConfiguration(ConcordConfiguration& config, Iterator iterator,
                         Iterator end, log4cplus::Logger* errorOut = nullptr,
                         bool overwrite = true) {
    if (!success) {
      return false;
    }
    while (iterator != end) {
      if (config.contains(*iterator)) {
        loadParameter(config, *iterator, yaml, errorOut, overwrite);
      }
      ++iterator;
    }
    return true;
  }
};

// This class handles serializing ConcordConfiguration values to a YAML file. It
// is intended to be capable of writing configuration files readable by the
// YAMLConfigurationInput class above. As such, it represents
// ConcordConfiguration values in YAML using the same format outlined in the
// documentation of YAMLConfigurationInput above.
//
// This class currently does not provide any synchronization or guarantees of
// synchronization.
class YAMLConfigurationOutput {
 private:
  std::ostream* output;
  YAML::Node yaml;

  void addParameterToYAML(const ConcordConfiguration& config,
                          const ConfigurationPath& path, YAML::Node& yaml);

 public:
  YAMLConfigurationOutput(std::ostream& output);
  ~YAMLConfigurationOutput();

  // Writes the selected values from the given configuration to the ostream this
  // YAMLConfigurationOutput was constructed with. If this function is
  // successful (i.e. an exception does not occur), it will output YAML for
  // every parameer returned by iterator that config has a value for.
  // Parameters:
  // - config: ConcordConfiguration from which to write the selected values.
  // - iterator: Iterator returning ConfigurationPaths or ConfigurationPath
  // references of paths to parameters within config to write the values of.
  // - end: Iterator to the end of the range of ConfigurationPaths requested,
  // used to tell when iterator has finished iterating.
  // Throws: Any exception that occurs while attempting the requested output
  // operation. It is anticipated this could include any I/O exceptions that
  // occur while trying to write to the std::ostrem this YAMLConfigurationOutput
  // was constructed with.
  template <class Iterator>
  void outputConfiguration(ConcordConfiguration& config, Iterator iterator,
                           Iterator end) {
    yaml.reset(YAML::Node(YAML::NodeType::Map));
    while (iterator != end) {
      if (config.hasValue<std::string>(*iterator)) {
        addParameterToYAML(config, *iterator, yaml);
      }
      ++iterator;
    }
    (*output) << yaml;
  }
};

// Current auxiliary state to the main ConcordConfiguration, containing objects
// that we would like to give the configuration ownership of for purposes of
// memory management. At the time of this writing, this primarily consists of
// cryptographic state.
struct ConcordPrimaryConfigurationAuxiliaryState
    : public ConfigurationAuxiliaryState {
  std::unique_ptr<Cryptosystem> slowCommitCryptosys;
  std::unique_ptr<Cryptosystem> commitCryptosys;
  std::unique_ptr<Cryptosystem> optimisticCommitCryptosys;

  std::vector<std::pair<std::string, std::string>> replicaRSAKeys;

  ConcordPrimaryConfigurationAuxiliaryState();
  virtual ~ConcordPrimaryConfigurationAuxiliaryState();
  virtual ConfigurationAuxiliaryState* clone();
};

const unsigned int kRSAKeyLength = 2048;

// Generates an RSA private/public key pair for use within Concord. This
// function is used primarily at configuration generation time, and, at the time
// of this writing, this function generally should not be used elsewhere within
// Concord's actual code, however, we are currently exposing this functionally
// to facilitate unit testing of components using RSA keys without requiring
// those unit tests to configure an entire Concord cluster. Note the keys are
// returned through an std::pair in the order (private key, public key).
std::pair<std::string, std::string> generateRSAKeyPair(
    CryptoPP::RandomPool& randomnessSource);

// Builds a ConcordConfiguration object to contain the definition of the current
// configuration we are using. This function is intended to serve as a single
// source of truth for all places that need to know the current Concord
// configuration format. At the time of this writing, that includes both the
// configuration generation utility and configuration loading in Concord
// replicas.
//
// This function accepts the ConcordConfiguration it will be initializing by
// reference; it clears any existing contents of the ConcordConfiguration it is
// given before filling it with the configuration defintion.
//
// If you need to add new configuration parameters or otherwise change the
// format of our configuration files, you should add to or modify the code in
// this function's implementation.
void specifyConfiguration(ConcordConfiguration& config);

// Additional useful functions containing encoding knowledge about the current
// configuration.

// Loads all parameters necessary to size a concord cluster to the given
// ConcordConfiguration from the given YAMLConfigurationInput. This function
// expects that config was initialized with specifyConfiguration; this function
// also expects the YAMLConfigurationInput has already parsed and cached the
// YAML for its input file, and that the. Throws a
// ConfigurationResourceNotFoundException if values for any required cluster
// sizing parameters cannot be found in the input or cannot be loaded to the
// configuration.
void loadClusterSizeParameters(YAMLConfigurationInput& input,
                               ConcordConfiguration& config);

// Instantiates the scopes within the given concord node configuration. This
// function expects that config was initialized with specifyConfiguration. This
// function requires that all parameters needed to compute configuration scope
// sizes have already been loaded, which can be done with
// loadClusterSizeParameters. Furthermore, this function expects that input has
// already parsed and cached the YAML values for its input file. Throws a
// ConfigurationResourceNotFoundException if any parameters required to size the
// cluster have not had values loaded to the configuration.
//
// If input contains any values for parameters in scope templates, those
// parameters will also be loaded and propogated to the created instances. We
// choose to integrate the processes for loading values of parameters in
// templates and intantiating the templates into a single function to facilitate
// correctly handling cases where parameter values are given in places that mix
// scope templates and instances (For example, if the input includes a setting
// specific to the first client proxy on each node).
void instantiateTemplatedConfiguration(YAMLConfigurationInput& input,
                                       ConcordConfiguration& config);

// Loads all non-generated (i.e. required and optional input) parameters from
// the given YAMLConfigurationInput to the given ConcordConfiguration object.
// This function expects that config was initialized with specifyConfiguration
// and that its scopes have already been instantiated to the correct sizes for
// this configuration (which can be done with
// instantiateTemplatedConfiguration). Furthermore, it is expected that the
// YAMLConfigurationInput given has already parsed its input file and cached the
// YAML values. This function will throw a
// ConfigurationResourceNotFoundException if any required input parameter is not
// available in the input.
void loadConfigurationInputParameters(YAMLConfigurationInput& input,
                                      ConcordConfiguration& config);

// Runs key generation for all RSA and threshold cryptographic keys required by
// the Concord-BFT replicas in this Concord cluster. It should be noted that
// this does not cause the generated keys to be loaded to the
// ConcordConfiguration object; they are stored in the ConcordConfiguration's
// auxiliary state. We have chosen to generate the keys in this separate step
// rather than having the parameter generation functions the
// ConcordConfiguration is constructed with generate them in order to simplify
// things, given that certain pairs of keys must be generated at the same time
// (for example, a replica's public RSA key must be generated with its private
// key). This function expects the given ConcordConfiguration to have been
// initialized with specifyConfiguration and to have had its scopes instantiated
// to the correct size, which can be done with
// instantiateTemplatedConfiguration. Furthermore, values must be loaded to the
// configuration for all required cluster size parameters and cryptosystem
// selection parameters. A ConfigurationResourceNotFoundException will be thrown
// if any such parameters needed by this function are missing.
void generateConfigurationKeys(ConcordConfiguration& config);

// Validates that the given ConcordConfiguration has values for all parameters
// that are needed in the configuration files; returns true if this is the case
// and false otherwise. This function expects that config was initialized with
// specifyConfiguration.
bool hasAllParametersRequiredAtConfigurationGeneration(
    ConcordConfiguration& config);

// Uses the provided YAMLConfigurationOutput to write a configuration file for a
// specified node. This function expects that values for all parameters that are
// desired in the output have already been loaded; it will only output values
// for parameters that are both initialized in config and that should be
// included in the given replica's configuration file. Note this function may
// throw any I/O or YAML serialization exceptions that occur while attempting
// this operation.
//
// Note that, at the time of this writing, Concord supports a feature (see the
// use_loopback_for_local_hosts configuration parameter) whereby loopback
// IPs (i.e. 127.0.0.1) are inserted into each node's configuration file for
// hosts used for internal Concord communication which are on that node. Note
// that, if this feature is enabled in config, this function will handle
// substitution of the loopback IPs as appropriate in the configuration file it
// outputs. Throws an std::invalid_argument if this feature is enabled but the
// loopback IP is rejected by the node's configuration object.
void outputConcordNodeConfiguration(const ConcordConfiguration& config,
                                    YAMLConfigurationOutput& output,
                                    size_t node);

// Loads the Concord configuration for a node (presumably the one running this
// function) from a specified configuration file. This function expects that
// config has been initilized with specifyConfiguration (see above); this
// function also expects that the YAMLConfigurationInput has already parsed its
// input file and cached the YAML values. Furthermore, this function expects
// that the logging system has been initialized so that it can log any issues it
// finds with the configuration. This function will throw a
// ConfigurationResourceNotFoundException if any required configuration
// parameters are missing from the input.
void loadNodeConfiguration(ConcordConfiguration& config,
                           YAMLConfigurationInput& input);

// Detects which node the given config is for. This function expects that the
// given config has been initialized with specifyConfiguration (see above) and
// that the configuration of interest was already loaded to it. This function
// will throw a ConfigurationResourceNotFoundException if it cannot determine
// which node the configuration is for.
size_t detectLocalNode(ConcordConfiguration& config);

// Initializes the cryptosystems in the given ConcordConfiguration's auxiliary
// state and load the keys contained in the configuration to them so they can be
// used in initializing SBFT to create threshold signers and verifiers. Note
// this function expects that config was initialized with specifyConfiguration
// (see above) and that the entire node configuration has already been loaded
// (this can be done with loadNodeConfiguration (see above)). This function may
// throw a ConfigurationResourceNotFoundException if it detects that any
// information it requires from the configuration is not available.
void loadSBFTCryptosystems(ConcordConfiguration& config);

// Outputs, to output, a mapping represented in JSON reporting which Concord-BFT
// principals (by principal ID) are on each Concord node in the cluster
// configured by config. The JSON output will be a single JSON object. For each
// node in the cluster, this object will contain one name-value pair, where the
// name is a (base 10) string representation of the index of a node in the
// Concord cluster, and the value is an array of numbers, where the numbers are
// the principal IDs of the Concord-BFT principals that have been placed on that
// node. Note the node IDs used are 1-indexed; this is done to make them
// consistent with the Concord configuration file indexes output in the
// filenames generated by conc_genconfig at the time of this writing. Note this
// function does not itself verify that config is a valid configuration and has
// all expected nodes and principal IDs; any missing information will be omitted
// from the output. Note this function may throw any I/O or JSON serializaation
// exceptions that occur while attempting to output the requested file.
void outputPrincipalLocationsMappingJSON(ConcordConfiguration& config,
                                         std::ostream& output);

// Declaration and implementation of various functions used by
// specifyConfiguration to specify how to size scopes and validatate and
// generate parameters. Pointers to these functions are given to the
// ConcordConfiguration object specifyConfiguration builds so that the
// configuration system can call them at the appropriate time (for example,
// parameter validators are called automatically when parameters are loaded).
// Computes the total number of Concord nodes. Note we currently assume there is
// one Concord node per SBFT replica, and the SBFT algorithm specifies that the
// cluster size in terms of its F and C parameters is (3F + 2C + 1) replicas.
ConcordConfiguration::ParameterStatus sizeNodes(
    const ConcordConfiguration& config, const ConfigurationPath& path,
    size_t* output, void* state);

// Computes the number of SBFT replicas per Concord node. Note that, at the time
// of this writing, we assume there is exactly one SBFT replica per Concord
// node.
ConcordConfiguration::ParameterStatus sizeReplicas(
    const ConcordConfiguration& config, const ConfigurationPath& path,
    size_t* output, void* state);

// Validate an unsigned integer.
ConcordConfiguration::ParameterStatus validateUInt(
    const std::string& value, const ConcordConfiguration& config,
    const ConfigurationPath& path, std::string* failureMessage, void* state);

inline const std::pair<unsigned long long, unsigned long long>
    kPositiveIntLimits({1, INT_MAX});
inline const std::pair<unsigned long long, unsigned long long>
    kPositiveUInt16Limits({1, UINT16_MAX});
inline const std::pair<unsigned long long, unsigned long long>
    kPositiveUInt64Limits({1, UINT64_MAX});
inline const std::pair<unsigned long long, unsigned long long>
    kPositiveULongLongLimits({1, ULLONG_MAX});
inline const std::pair<unsigned long long, unsigned long long> kUInt16Limits(
    {0, UINT16_MAX});
inline const std::pair<unsigned long long, unsigned long long> kUInt32Limits(
    {0, UINT32_MAX});
inline const std::pair<unsigned long long, unsigned long long> kUInt64Limits(
    {0, UINT64_MAX});
inline const std::pair<long long, long long> kInt32Limits({INT32_MIN,
                                                           INT32_MAX});

// We enforce a minimum size on communication buffers to ensure at least
// minimal error responses can be passed through them.
inline const std::pair<unsigned long long, unsigned long long>
    kConcordBFTCommunicationBufferSizeLimits({512, UINT32_MAX});

}  // namespace config
}  // namespace concord

// Parse Concord's command line options (storing them to opts_out) and load its
// configuration from the configuration file specified by the command line to
// config_out. Returns true if Concord's configuration was loaded successfully,
// or if the command line invokes Concord in a recognized way not requiring a
// configuration (ex: concord --help), and returns false otherwise.
bool initialize_config(int agrc, char** argv,
                       concord::config::ConcordConfiguration& config_out,
                       boost::program_options::variables_map& opts_out);

#endif  // CONFIG_CONFIGURATION_MANAGER_HPP
