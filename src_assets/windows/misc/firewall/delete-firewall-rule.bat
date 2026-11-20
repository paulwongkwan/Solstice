@echo off

set RULE_NAME=Solstice

rem Delete the rule
netsh advfirewall firewall delete rule name=%RULE_NAME%
