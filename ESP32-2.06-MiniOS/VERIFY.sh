#!/bin/bash
# Verification script for 2.06" port

echo "═══════════════════════════════════════════════════════"
echo " S3 MiniOS 2.06\" Port Verification"
echo "═══════════════════════════════════════════════════════"

SUCCESS=0
FAIL=0

# Test 1: Check for XCA9554 code (should only be in comments)
echo -n "✓ Checking XCA9554 code removal... "
if grep -E "XCA9554_(ADDR|class|expander\.|\.pinMode|\.digitalWrite)" S3_MiniOS.ino 2>/dev/null; then
    echo "❌ FAIL - XCA9554 code found"
    FAIL=$((FAIL+1))
else
    echo "✅ PASS"
    SUCCESS=$((SUCCESS+1))
fi

# Test 2: Check display driver
echo -n "✓ Checking CO5300 display driver... "
if grep -q "Arduino_CO5300" S3_MiniOS.ino; then
    echo "✅ PASS"
    SUCCESS=$((SUCCESS+1))
else
    echo "❌ FAIL - CO5300 driver not found"
    FAIL=$((FAIL+1))
fi

# Test 3: Check resolution
echo -n "✓ Checking 410×502 resolution... "
if grep -q "LCD_WIDTH.*410" pin_config.h && grep -q "LCD_HEIGHT.*502" pin_config.h; then
    echo "✅ PASS"
    SUCCESS=$((SUCCESS+1))
else
    echo "❌ FAIL - Wrong resolution"
    FAIL=$((FAIL+1))
fi

# Test 4: Check touch interrupt pin
echo -n "✓ Checking TP_INT GPIO 38... "
if grep -q "TP_INT.*38" pin_config.h; then
    echo "✅ PASS"
    SUCCESS=$((SUCCESS+1))
else
    echo "❌ FAIL - Wrong touch interrupt pin"
    FAIL=$((FAIL+1))
fi

# Test 5: Check LCD reset pin
echo -n "✓ Checking LCD_RESET GPIO 8... "
if grep -q "LCD_RESET.*8" pin_config.h; then
    echo "✅ PASS"
    SUCCESS=$((SUCCESS+1))
else
    echo "❌ FAIL - LCD_RESET not defined"
    FAIL=$((FAIL+1))
fi

# Test 6: Check touch reset pin
echo -n "✓ Checking TP_RESET GPIO 9... "
if grep -q "TP_RESET.*9" pin_config.h; then
    echo "✅ PASS"
    SUCCESS=$((SUCCESS+1))
else
    echo "❌ FAIL - TP_RESET not defined"
    FAIL=$((FAIL+1))
fi

# Test 7: Check I2S pins update
echo -n "✓ Checking I2S BCK GPIO 41... "
if grep -q "I2S_BCK_IO.*41" pin_config.h; then
    echo "✅ PASS"
    SUCCESS=$((SUCCESS+1))
else
    echo "❌ FAIL - I2S BCK pin not updated"
    FAIL=$((FAIL+1))
fi

# Test 8: Check reset initialization in setup
echo -n "✓ Checking reset pin initialization... "
if grep -q "pinMode(LCD_RESET, OUTPUT)" S3_MiniOS.ino && \
   grep -q "pinMode(TP_RESET, OUTPUT)" S3_MiniOS.ino; then
    echo "✅ PASS"
    SUCCESS=$((SUCCESS+1))
else
    echo "❌ FAIL - Reset pins not initialized"
    FAIL=$((FAIL+1))
fi

# Test 9: Check required files exist
echo -n "✓ Checking required files... "
if [ -f "S3_MiniOS.ino" ] && [ -f "pin_config.h" ] && \
   [ -f "README.md" ] && [ -d "wifi" ]; then
    echo "✅ PASS"
    SUCCESS=$((SUCCESS+1))
else
    echo "❌ FAIL - Missing files"
    FAIL=$((FAIL+1))
fi

# Summary
echo ""
echo "═══════════════════════════════════════════════════════"
echo " Results: $SUCCESS PASS | $FAIL FAIL"
echo "═══════════════════════════════════════════════════════"

if [ $FAIL -eq 0 ]; then
    echo "✅ All verification checks passed!"
    echo "   Ready to compile and upload to 2.06\" board."
    exit 0
else
    echo "❌ Some checks failed. Review changes needed."
    exit 1
fi
