*** Settings ***
Suite Setup       Setup
Suite Teardown    Teardown
Test Teardown     Test Teardown
Library           scenario.robot_library.ScenarioLibrary

*** Variables ***
${MACHINE_SCRIPT}    ${CURDIR}/../build/student/boot.resc
${SCENARIO}          ${CURDIR}/scenarios/normal.json
${SCENARIO_RESULT}   ${CURDIR}/../build/scenario-result.json

*** Test Cases ***
Versioned parking scenario obeys peripheral behavior
    Execute Scenario    ${SCENARIO}    ${MACHINE_SCRIPT}    ${SCENARIO_RESULT}
