/*
Copyright 2015 University of Auckland

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

#include "libcellml/validator.h"

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include "libcellml/component.h"
#include "libcellml/import.h"
#include "libcellml/error.h"
#include "libcellml/model.h"
#include "libcellml/units.h"
#include "libcellml/variable.h"
#include "xmldoc.h"

namespace libcellml {

/**
 * @brief Map StandardUnit to their string forms.
 *
 * An internal map used to convert a standard unit into its string form.
 */
std::map<Units::StandardUnit, const std::string> standardUnitToStringTemp =
{
    {Units::StandardUnit::AMPERE, "ampere"},
    {Units::StandardUnit::BECQUEREL, "becquerel"},
    {Units::StandardUnit::CANDELA, "candela"},
    {Units::StandardUnit::CELSIUS, "celsius"},
    {Units::StandardUnit::COULOMB, "coulomb"},
    {Units::StandardUnit::DIMENSIONLESS, "dimensionless"},
    {Units::StandardUnit::FARAD, "farad"},
    {Units::StandardUnit::GRAM, "gram"},
    {Units::StandardUnit::GRAY, "gray"},
    {Units::StandardUnit::HENRY, "henry"},
    {Units::StandardUnit::HERTZ, "hertz"},
    {Units::StandardUnit::JOULE, "joule"},
    {Units::StandardUnit::KATAL, "katal"},
    {Units::StandardUnit::KELVIN, "kelvin"},
    {Units::StandardUnit::KILOGRAM, "kilogram"},
    {Units::StandardUnit::LITER, "liter"},
    {Units::StandardUnit::LITRE, "litre"},
    {Units::StandardUnit::LUMEN, "lumen"},
    {Units::StandardUnit::LUX, "lux"},
    {Units::StandardUnit::METER, "meter"},
    {Units::StandardUnit::METRE, "metre"},
    {Units::StandardUnit::MOLE, "mole"},
    {Units::StandardUnit::NEWTON, "newton"},
    {Units::StandardUnit::OHM, "ohm"},
    {Units::StandardUnit::PASCAL, "pascal"},
    {Units::StandardUnit::RADIAN, "radian"},
    {Units::StandardUnit::SECOND, "second"},
    {Units::StandardUnit::SIEMENS, "siemens"},
    {Units::StandardUnit::SIEVERT, "sievert"},
    {Units::StandardUnit::STERADIAN, "steradian"},
    {Units::StandardUnit::TESLA, "tesla"},
    {Units::StandardUnit::VOLT, "volt"},
    {Units::StandardUnit::WATT, "watt"},
    {Units::StandardUnit::WEBER, "weber"}
};

/**
 * @brief The Validator::ValidatorImpl struct.
 *
 * The private implementation for the Validator class.
 */
struct Validator::ValidatorImpl
{
public:
    // TODO: doc
    Validator *mValidator;
    void validateMath(const std::string &input, const ComponentPtr &component, std::vector<std::string> &variableNames);
private:
    void gatherMathBvarVariableNames(XmlNodePtr &node, std::vector<std::string> &bvarNames);
    void validateAndCleanMathCiCnNodes(XmlNodePtr &node, const ComponentPtr &component, const std::vector<std::string> &variableNames, const std::vector<std::string> &bvarNames);
    bool isNotWhitespace(std::string &input);
    void removeSubstring(std::string &input, std::string &pattern);
};

Validator::Validator()
    : mPimpl(new ValidatorImpl())
{
    mPimpl->mValidator = this;
}

Validator::~Validator()
{
    delete mPimpl;
}

Validator::Validator(const Validator& rhs)
    : Logger(rhs)
    , mPimpl(new ValidatorImpl())
{
    mPimpl->mValidator = rhs.mPimpl->mValidator;
}

Validator::Validator(Validator &&rhs)
    : Logger(std::move(rhs))
    , mPimpl(rhs.mPimpl)
{
    rhs.mPimpl = nullptr;
}

Validator& Validator::operator=(Validator v)
{
    Logger::operator =(v);
    v.swap(*this);
    return *this;
}

void Validator::swap(Validator &rhs)
{
    std::swap(this->mPimpl, rhs.mPimpl);
}

void Validator::validateModel(const ModelPtr &model)
{
    // Clear any pre-existing errors in ths validator instance.
    clearErrors();
    // Check for a valid name attribute.
    if (!model->getName().length()) {
        ErrorPtr err = std::make_shared<Error>();
        err->setDescription("Model does not have a valid name attribute.");
        err->setModel(model);
        err->setKind(Error::Kind::MODEL);
        addError(err);
    }
    // Check for components in this model.
    if (model->componentCount() > 0) {
        std::vector<std::string> componentNames;
        std::vector<std::string> componentRefs;
        std::vector<std::string> componentImportSources;
        for (size_t i = 0; i < model->componentCount(); ++i) {
            ComponentPtr component = model->getComponent(i);
            // Check for duplicate component names in this model.
            std::string componentName = component->getName();
            if (componentName.length()) {
                if (component->isImport()) {
                    // Check for a component_ref.
                    std::string componentRef = component->getImportReference();
                    std::string importSource = component->getImport()->getSource();
                    bool foundImportError = false;
                    if (!componentRef.length()) {
                        ErrorPtr err = std::make_shared<Error>();
                        err->setDescription("Imported component '" + componentName +
                                            "' does not have a valid component_ref attribute.");
                        err->setComponent(component);
                        err->setKind(Error::Kind::COMPONENT);
                        addError(err);
                        foundImportError = true;
                    }
                    // Check for a xlink:href.
                    if (!importSource.length()) {
                        ErrorPtr err = std::make_shared<Error>();
                        err->setDescription("Import of component '" + componentName +
                                            "' does not have a valid locator xlink:href attribute.");
                        err->setImport(component->getImport());
                        err->setKind(Error::Kind::IMPORT);
                        addError(err);
                        foundImportError = true;
                    }
                    // Check if we already have another import from the same source with the same component_ref.
                    // (This looks for matching entries at the same position in the source and ref vectors).
                    if ((componentImportSources.size() > 0) && (!foundImportError)) {
                        if ((std::find(componentImportSources.begin(), componentImportSources.end(), importSource) - componentImportSources.begin())
                         == (std::find(componentRefs.begin(), componentRefs.end(), componentRef) - componentRefs.begin())){
                            ErrorPtr err = std::make_shared<Error>();
                            err->setDescription("Model '" + model->getName() +
                                                "' contains multiple imported components from '" + importSource +
                                                "' with the same component_ref attribute '" + componentRef + "'.");
                            err->setModel(model);
                            err->setKind(Error::Kind::MODEL);
                            addError(err);
                        }
                    }
                    // Push back the unique sources and refs.
                    componentImportSources.push_back(importSource);
                    componentRefs.push_back(componentRef);
                }
                if(std::find(componentNames.begin(), componentNames.end(), componentName) != componentNames.end()) {
                    ErrorPtr err = std::make_shared<Error>();
                    err->setDescription("Model '" + model->getName() +
                                        "' contains multiple components with the name '" + componentName +
                                        "'. Valid component names should be unique to their model.");
                    err->setModel(model);
                    err->setKind(Error::Kind::MODEL);
                    addError(err);
                }
                componentNames.push_back(componentName);
            }
            // Validate component.
            validateComponent(component);
        }
    }
    // Check for units in this model.
    if (model->unitsCount() > 0) {
        std::vector<std::string> unitsNames;
        std::vector<std::string> unitsRefs;
        std::vector<std::string> unitsImportSources;
        for (size_t i = 0; i < model->unitsCount(); ++i) {
            UnitsPtr units = model->getUnits(i);
            std::string unitsName = units->getName();
            if (unitsName.length()) {
                if (units->isImport()) {
                    // Check for a units_ref.
                    std::string unitsRef = units->getImportReference();
                    std::string importSource = units->getImport()->getSource();
                    bool foundImportError = false;
                    if (!unitsRef.length()) {
                        ErrorPtr err = std::make_shared<Error>();
                        err->setDescription("Imported units '" + unitsName +
                                            "' does not have a valid units_ref attribute.");
                        err->setUnits(units);
                        err->setKind(Error::Kind::UNITS);
                        addError(err);
                        foundImportError = true;
                    }
                    // Check for a xlink:href.
                    if (!importSource.length()) {
                        ErrorPtr err = std::make_shared<Error>();
                        err->setDescription("Import of units '" + unitsName +
                                            "' does not have a valid locator xlink:href attribute.");
                        err->setImport(units->getImport());
                        err->setKind(Error::Kind::IMPORT);
                        addError(err);
                        foundImportError = true;
                    }
                    // Check if we already have another import from the same source with the same units_ref.
                    // (This looks for matching enties at the same position in the source and ref vectors).
                    if ((unitsImportSources.size() > 0) && (!foundImportError)) {
                        if ((std::find(unitsImportSources.begin(), unitsImportSources.end(), importSource) - unitsImportSources.begin())
                         == (std::find(unitsRefs.begin(), unitsRefs.end(), unitsRef) - unitsRefs.begin())){
                            ErrorPtr err = std::make_shared<Error>();
                            err->setDescription("Model '" + model->getName() +
                                                "' contains multiple imported units from '" + importSource +
                                                "' with the same units_ref attribute '" + unitsRef + "'.");
                            err->setModel(model);
                            err->setKind(Error::Kind::MODEL);
                            addError(err);
                        }
                    }
                    // Push back the unique sources and refs.
                    unitsImportSources.push_back(importSource);
                    unitsRefs.push_back(unitsRef);
                }
                // Check for duplicate units names in this model.
                if(std::find(unitsNames.begin(), unitsNames.end(), unitsName) != unitsNames.end()) {
                    ErrorPtr err = std::make_shared<Error>();
                    err->setDescription("Model '" + model->getName() +
                                        "' contains multiple units with the name '" + unitsName +
                                        "'. Valid units names should be unique to their model.");
                    err->setModel(model);
                    err->setKind(Error::Kind::MODEL);
                    addError(err);
                }
                unitsNames.push_back(unitsName);
            }
        }
        for (size_t i = 0; i < model->unitsCount(); ++i) {
            // Validate units.
            UnitsPtr units = model->getUnits(i);
            validateUnits(units, unitsNames);
        }
    }
}

void Validator::validateComponent(const ComponentPtr &component)
{
    // Check for a valid name attribute.
    if (!component->getName().length()) {
        ErrorPtr err = std::make_shared<Error>();
        err->setDescription("Component does not have a valid name attribute.");
        err->setComponent(component);
        err->setKind(Error::Kind::COMPONENT);
        addError(err);
    }
    // Check for units in this component.
    if (component->unitsCount() > 0) {
        std::vector<std::string> unitsNames;
        for (size_t i = 0; i < component->unitsCount(); ++i) {
            UnitsPtr units = component->getUnits(i);
            // Check for duplicate units names in this component.
            std::string unitsName = units->getName();
            if (unitsName.length()) {
                if(std::find(unitsNames.begin(), unitsNames.end(), unitsName) != unitsNames.end()) {
                    ErrorPtr err = std::make_shared<Error>();
                    err->setDescription("Component '" + component->getName() +
                                        "' contains multiple units with the name '" + unitsName +
                                        "'. Valid units names should be unique to their component.");
                    err->setComponent(component);
                    err->setKind(Error::Kind::COMPONENT);
                    addError(err);
                }
                unitsNames.push_back(unitsName);
            }
        }
        for (size_t i = 0; i < component->unitsCount(); ++i) {
            // Validate units.
            UnitsPtr units = component->getUnits(i);
            validateUnits(units, unitsNames);
        }
    }
    // Check for variables in this component.
    std::vector<std::string> variableNames;
    if (component->variableCount() > 0) {
        // Check for duplicate variable names and construct vector of valid names in case
        // we have a variable initial_value set by reference.
        for (size_t i = 0; i < component->variableCount(); ++i) {
            std::string variableName = component->getVariable(i)->getName();
            if (variableName.length()) {
                if(std::find(variableNames.begin(), variableNames.end(), variableName) != variableNames.end()) {
                    ErrorPtr err = std::make_shared<Error>();
                    err->setDescription("Component '" + component->getName() +
                                        "' contains multiple variables with the name '" + variableName +
                                        "'. Valid variable names should be unique to their component.");
                    err->setComponent(component);
                    err->setKind(Error::Kind::COMPONENT);
                    addError(err);
                }
                variableNames.push_back(variableName);
            }
        }
        // Validate variable(s).
        for (size_t i = 0; i < component->variableCount(); ++i) {
            VariablePtr variable = component->getVariable(i);
            validateVariable(variable, variableNames);
        }
    }
    // Validate math through the private implementation (for XML handling).
    if (component->getMath().length()) {
        mPimpl->validateMath(component->getMath(), component, variableNames);
    }
}

void Validator::validateUnits(const UnitsPtr &units, const std::vector<std::string> unitsNames)
{
    // Check for a valid name attribute.
    if (!units->getName().length()) {
        ErrorPtr err = std::make_shared<Error>();
        err->setDescription("Units does not have a valid name attribute.");
        err->setUnits(units);
        err->setKind(Error::Kind::UNITS);
        addError(err);
    } else {
        // Check that the units is not a Standard Unit. Assemble a vector of standard units
        std::vector<std::string> standardUnitNames;
        for (Units::StandardUnit s = Units::StandardUnit::AMPERE; s <= Units::StandardUnit::WEBER; s = Units::StandardUnit(int(s) + 1)) {
            standardUnitNames.push_back(standardUnitToStringTemp.find(s)->second);
        }
        // Check for a matching standard units.
        if (std::find(standardUnitNames.begin(), standardUnitNames.end(), units->getName()) != standardUnitNames.end()) {
            ErrorPtr err = std::make_shared<Error>();
            err->setDescription("Units is named '" + units->getName() +
                                "', which is a protected standard unit name.");
            err->setUnits(units);
            err->setKind(Error::Kind::UNITS);
            addError(err);
        }
    }
    // Validate each unit in units.
    // TODO: move unit validation into validator.
    std::vector<std::string> unitErrors = units->getUnitValidationErrors(unitsNames);
    for (size_t i = 0; i < unitErrors.size(); ++i) {
        ErrorPtr err = std::make_shared<Error>();
        err->setDescription(unitErrors[i]);
        err->setUnits(units);
        err->setKind(Error::Kind::UNITS);
        addError(err);
    }

}

void Validator::validateVariable(const VariablePtr &variable, std::vector<std::string> &variableNames)
{
    // Check for a valid name attribute.
    if (!variable->getName().length()) {
        ErrorPtr err = std::make_shared<Error>();
        err->setDescription("Variable does not have a valid name attribute.");
        err->setVariable(variable);
        err->setKind(Error::Kind::VARIABLE);
        addError(err);
    }
    // Check for a valid units attribute.
    if (!variable->getUnits().length()) {
        ErrorPtr err = std::make_shared<Error>();
        err->setDescription("Variable '" + variable->getName() +
                            "' does not have a valid units attribute.");
        err->setVariable(variable);
        err->setKind(Error::Kind::VARIABLE);
        addError(err);
    }
    // Check for a valid interface attribute.
    if (variable->getInterfaceType().length()) {
        std::string interfaceType = variable->getInterfaceType();
        if ((interfaceType != "public") && (interfaceType != "private") &&
            (interfaceType != "none") && (interfaceType != "public_and_private")) {
            ErrorPtr err = std::make_shared<Error>();
            err->setDescription("Variable '" + variable->getName() +
                                "' has an invalid interface attribute value '" + interfaceType + "'.");
            err->setVariable(variable);
            err->setKind(Error::Kind::VARIABLE);
            addError(err);
        }
    }
    // Check for a valid initial value attribute.
    if (variable->getInitialValue().length()) {
        std::string initialValue = variable->getInitialValue();
        // Check if initial value is a variable reference
        if(!(std::find(variableNames.begin(), variableNames.end(), initialValue) != variableNames.end())) {
            // Otherwise, check that the initial value can be converted to a double
            if (catchDoubleConversionError(initialValue)) {
                ErrorPtr err = std::make_shared<Error>();
                err->setDescription("Variable '" + variable->getName() +
                                    "' has an invalid initial value '" + initialValue +
                                    "'. Initial values must be a real number string or a variable reference.");
                err->setVariable(variable);
                err->setKind(Error::Kind::VARIABLE);
                addError(err);
            }
        }
    }
}

void Validator::ValidatorImpl::validateMath(const std::string &input, const ComponentPtr &component, std::vector<std::string> &variableNames)
{
    XmlDocPtr doc = std::make_shared<XmlDoc>();
    // Parse as XML first.
    doc->parse(input);
    // Copy any XML parsing errors into the common validator error handler.
    if (doc->xmlErrorCount() > 0) {
        for (size_t i = 0; i < doc->xmlErrorCount(); ++i) {
            ErrorPtr err = std::make_shared<Error>();
            err->setDescription(doc->getXmlError(i));
            err->setKind(Error::Kind::XML);
            mValidator->addError(err);
        }
    }
    XmlNodePtr node = doc->getRootNode();
    if (!node) {
        ErrorPtr err = std::make_shared<Error>();
        err->setDescription("Could not get a valid XML root node from the math on component '" + component->getName() + "'.");
        err->setKind(Error::Kind::XML);
        err->setComponent(component);
        mValidator->addError(err);
        return;
    } else if (!node->isType("math")) {
        ErrorPtr err = std::make_shared<Error>();
        err->setDescription("Math root node is of invalid type '" + node->getType() +
                            "' on component '" + component->getName() +
                            "'. A valid math root node should be of type 'math'.");
        err->setComponent(component);
        err->setKind(Error::Kind::XML);
        mValidator->addError(err);
        return;
    }
    XmlNodePtr nodeCopy = node;
    std::vector<std::string> bvarNames;
    // Get the bvar names in this math element.
    // TODO: may want to do this with XPath instead...
    gatherMathBvarVariableNames(nodeCopy, bvarNames);
    // Check that no variable names match new bvar names.
    for (std::string &variableName : variableNames) {
        if (std::find(bvarNames.begin(), bvarNames.end(), variableName) != bvarNames.end())
        {
            ErrorPtr err = std::make_shared<Error>();
            err->setDescription("Math in component '" + component->getName() +
                                "' contains '" + variableName + "' as a bvar ci element but it is already a variable name.");
            err->setComponent(component);
            err->setKind(Error::Kind::MATHML);
            mValidator->addError(err);
        }
    }
    // Iterate through ci/cn elements and remove cellml units attributes.
    XmlNodePtr mathNode = node;
    validateAndCleanMathCiCnNodes(node, component, variableNames, bvarNames);
    // Get the MathML string (with cellml:units attributes already removed) and remove the CellML namespace.
    // While the removeSubstring() approach for removing the cellml namespace before validating with the MathML DTD
    // is not ideal, libxml does not appear to have a better way to remove a namespace declaration from the tree.
    std::string cellml2NamespaceString = std::string(" xmlns:cellml=\"http://www.cellml.org/cellml/2.0#\"");
    std::string cleanMathml = mathNode->convertToString();
    removeSubstring(cleanMathml, cellml2NamespaceString);

    // Parse/validate the clean math string with the W3C MathML DTD.
    XmlDocPtr mathmlDoc = std::make_shared<XmlDoc>();
    mathmlDoc->parseMathML(cleanMathml);
    // Copy any MathML validation errors into the common validator error handler.
    if (mathmlDoc->xmlErrorCount() > 0) {
        for (size_t i = 0; i < mathmlDoc->xmlErrorCount(); ++i) {
            ErrorPtr err = std::make_shared<Error>();
            err->setDescription(mathmlDoc->getXmlError(i));
            err->setComponent(component);
            err->setKind(Error::Kind::MATHML);
            mValidator->addError(err);
        }
    }
}

void Validator::ValidatorImpl::validateAndCleanMathCiCnNodes(XmlNodePtr &node, const ComponentPtr &component,
                                                   const std::vector<std::string> &variableNames,  const std::vector<std::string> &bvarNames)
{
    XmlNodePtr childNode = node->getFirstChild();
    std::string textNode;
    std::string nodeType = node->getType();
    if ((nodeType == "ci") || (nodeType == "cn")) {
        if (childNode) {
            if (childNode->isType("text")) {
                textNode = childNode->convertToString();
                if (isNotWhitespace(textNode)) {
                    if (nodeType == "ci") {
                        // Check whether we can find this text as a variable name in this component.
                        if ((std::find(variableNames.begin(), variableNames.end(), textNode) == variableNames.end()) &&
                            (std::find(bvarNames.begin(), bvarNames.end(), textNode) == bvarNames.end())) {
                            ErrorPtr err = std::make_shared<Error>();
                            err->setDescription("MathML ci element has the child text '" + textNode +
                                                "', which does not correspond with any variable names present in component '" + component->getName() +
                                                "' and is not a variable defined within a bvar element.");
                            err->setComponent(component);
                            err->setKind(Error::Kind::MATHML);
                            mValidator->addError(err);
                        }
                    } else if (nodeType == "cn") {
                        // Check whether the cn value can be safely coverted to a real number.
                        if (mValidator->catchDoubleConversionError(textNode)) {
                            ErrorPtr err = std::make_shared<Error>();
                            err->setDescription("MathML cn element has the value '" + textNode +
                                                "', which cannot be converted to a real number.");
                            err->setComponent(component);
                            err->setKind(Error::Kind::MATHML);
                            mValidator->addError(err);
                        }
                    }
                } else {
                    ErrorPtr err = std::make_shared<Error>();
                    err->setDescription("MathML " + nodeType + " element has a whitespace-only child element.");
                    err->setComponent(component);
                    err->setKind(Error::Kind::MATHML);
                    mValidator->addError(err);
                }
            }
        } else {
            ErrorPtr err = std::make_shared<Error>();
            err->setDescription("MathML " + nodeType + " element has no child.");
            err->setComponent(component);
            err->setKind(Error::Kind::MATHML);
            mValidator->addError(err);
        }
        // Get cellml:units attribute.
        XmlAttributePtr attribute = node->getFirstAttribute();
        std::string unitsName;
        XmlAttributePtr unitsAttribute;
        while (attribute) {
            if (attribute->getValue().length() > 0) {
                if (attribute->isType("units")) {
                    unitsName = attribute->getValue();
                    unitsAttribute = attribute;
                } else {
                    ErrorPtr err = std::make_shared<Error>();
                    err->setDescription("Math " + nodeType + " element has an invalid attribute type '" +
                                        attribute->getType() + "' in the cellml namespace.");
                    err->setComponent(component);
                    err->setKind(Error::Kind::MATHML);
                    mValidator->addError(err);
                }
            }
            attribute = attribute->getNext();
        }
        // Check that cellml:units has been set.
        if (!unitsName.length()) {
            if (nodeType == "cn") {
                ErrorPtr err = std::make_shared<Error>();
                err->setDescription("Math cn element with the value '" + textNode + "' does not have a cellml:units attribute.");
                err->setComponent(component);
                err->setKind(Error::Kind::MATHML);
                mValidator->addError(err);
            } else if (node->getParent()->isType("bvar")) {
                ErrorPtr err = std::make_shared<Error>();
                err->setDescription("Math bvar ci element with the value '" + textNode +
                                    "' does not have a valid cellml:units attribute.");
                err->setComponent(component);
                err->setKind(Error::Kind::MATHML);
                mValidator->addError(err);
            }
        // Check that a specified units is valid.
        } else {
            // Check for a matching units in this component.
            if (!component->hasUnits(unitsName)) {
                std::vector<std::string> standardUnitNames;
                for (Units::StandardUnit s = Units::StandardUnit::AMPERE; s <= Units::StandardUnit::WEBER; s = Units::StandardUnit(int(s) + 1)) {
                    standardUnitNames.push_back(standardUnitToStringTemp.find(s)->second);
                }
                // Check for a matching standard units.
                if (std::find(standardUnitNames.begin(), standardUnitNames.end(), unitsName) == standardUnitNames.end()) {
                    ErrorPtr err = std::make_shared<Error>();
                    err->setDescription("Math has a " + nodeType + " element with a cellml:units attribute '" + unitsName +
                                        "' that is not a valid reference to units in component '" +
                                        component->getName() + "' or a standard unit.");
                    err->setComponent(component);
                    err->setKind(Error::Kind::MATHML);
                    mValidator->addError(err);
                }
            }
        }
        // Now that we've validated this XML node's cellml:units attribute, remove it from the node.
        // This is done so we can validate a "clean" MathML string using the MathML DTD. The math
        // string stored on the component will not be affected.
        if (unitsAttribute) {
            unitsAttribute->removeAttribute();
        }
    } else {
        // Check children for ci/cn elements.
        if (childNode) {
            validateAndCleanMathCiCnNodes(childNode, component, variableNames, bvarNames);
        }
    }
    // Check siblings for ci/cn.
    node = node->getNext();
    if (node) {
        validateAndCleanMathCiCnNodes(node, component, variableNames, bvarNames);
    }
}

bool Validator::catchDoubleConversionError(const std::string &input)
{
    bool response = false;
    double value;
    // Try to convert the input string to double.
    try
    {
        value = std::stod(input);
    } catch (std::exception) {
        response = true;
        (void)value;
    }
    return response;
}

// TODO: We also use this in Parser- maybe should be a common method on Logger?
bool Validator::ValidatorImpl::isNotWhitespace (std::string &input)
{
    return input.find_first_not_of(" \t\n\v\f\r") != input.npos;
}

void Validator::ValidatorImpl::gatherMathBvarVariableNames(XmlNodePtr &node, std::vector<std::string> &bvarNames)
{
    XmlNodePtr childNode = node->getFirstChild();
    if (node->isType("bvar")) {
        if ((childNode) && (childNode->isType("ci"))) {
            XmlNodePtr grandchildNode = childNode->getFirstChild();
            if (grandchildNode) {
                std::string type = grandchildNode->getType();
                if (grandchildNode->isType("text")) {
                    std::string textNode = grandchildNode->convertToString();
                    if (isNotWhitespace(textNode)) {
                        bvarNames.push_back(textNode);
                    }
                }
            }
        }
    } else {
        // Check children for bvars.
        if (childNode) {
            gatherMathBvarVariableNames(childNode, bvarNames);
        }
    }
    // Check siblings for bvars.
    node = node->getNext();
    if (node) {
        gatherMathBvarVariableNames(node, bvarNames);
    }
}

void Validator::ValidatorImpl::removeSubstring(std::string &input, std::string &pattern) {
  std::string::size_type n = pattern.length();
  for (std::string::size_type i = input.find(pattern);
      i != std::string::npos;
      i = input.find(pattern))
      input.erase(i, n);
}

}
