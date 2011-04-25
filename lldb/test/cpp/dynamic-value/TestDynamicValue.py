"""
Use lldb Python API to test dynamic values in C++
"""

import os, time
import re
import unittest2
import lldb, lldbutil
from lldbtest import *

class DynamicValueTestCase(TestBase):

    mydir = os.path.join("cpp", "dynamic-value")

    @unittest2.skipUnless(sys.platform.startswith("darwin"), "requires Darwin")
    @python_api_test
    def test_get_dynamic_vals_with_dsym(self):
        """Test fetching C++ dynamic values from pointers & references."""
        self.buildDsym()
        self.do_get_dynamic_vals()

    @python_api_test
    def test_get_dynamic_vals_with_dwarf(self):
        """Test fetching C++ dynamic values from pointers & references."""
        self.buildDwarf()
        self.do_get_dynamic_vals()

    def setUp(self):
        # Call super's setUp().                                                                                                           
        TestBase.setUp(self)

        # Find the line number to break for main.c.                                                                                       

        self.do_something_line = line_number('pass-to-base.cpp', '// Break here in doSomething.')
        self.main_first_call_line = line_number('pass-to-base.cpp',
                                                 '// Break here and get real addresses of myB and otherB.')
        self.main_second_call_line = line_number('pass-to-base.cpp',
                                                       '// Break here and get real address of reallyA.')

    def examine_value_object_of_this_ptr (self, this_static, this_dynamic, dynamic_location):

        # Get "this" as its static value
        
        self.assertTrue (this_static.IsValid())
        this_static_loc = int (this_static.GetValue(), 16)
        
        # Get "this" as its dynamic value
        
        self.assertTrue (this_dynamic.IsValid())
        this_dynamic_typename = this_dynamic.GetTypeName()
        self.assertTrue (this_dynamic_typename.find('B') != -1)
        this_dynamic_loc = int (this_dynamic.GetValue(), 16)
        
        # Make sure we got the right address for "this"
        
        self.assertTrue (this_dynamic_loc == dynamic_location)

        # And that the static address is greater than the dynamic one

        self.assertTrue (this_static_loc > this_dynamic_loc)
        
        # Now read m_b_value which is only in the dynamic value:

        this_dynamic_m_b_value = this_dynamic.GetChildMemberWithName('m_b_value', True)
        self.assertTrue (this_dynamic_m_b_value.IsValid())
        
        m_b_value = int (this_dynamic_m_b_value.GetValue(), 0)
        self.assertTrue (m_b_value == 10)
        
        # Make sure it is not in the static version

        this_static_m_b_value = this_static.GetChildMemberWithName('m_b_value', False)
        self.assertTrue (this_static_m_b_value.IsValid() == False)

        # Okay, now let's make sure that we can get the dynamic type of a child element:

        contained_auto_ptr = this_dynamic.GetChildMemberWithName ('m_client_A', True)
        self.assertTrue (contained_auto_ptr.IsValid())
        contained_b = contained_auto_ptr.GetChildMemberWithName ('_M_ptr', True)
        self.assertTrue (contained_b.IsValid())
        
        contained_b_static = contained_auto_ptr.GetChildMemberWithName ('_M_ptr', False)
        self.assertTrue (contained_b_static.IsValid())
        
        contained_b_addr = int (contained_b.GetValue(), 16)
        contained_b_static_addr = int (contained_b_static.GetValue(), 16)
        
        self.assertTrue (contained_b_addr < contained_b_static_addr)
        
    def do_get_dynamic_vals(self):
        """Get argument vals for the call stack when stopped on a breakpoint."""
        exe = os.path.join(os.getcwd(), "a.out")

        # Create a target from the debugger.

        target = self.dbg.CreateTarget (exe)
        self.assertTrue(target.IsValid(), VALID_TARGET)

        # Set up our breakpoints:

        do_something_bpt = target.BreakpointCreateByLocation('pass-to-base.cpp', self.do_something_line)
        self.assertTrue(do_something_bpt.IsValid() and
                        do_something_bpt.GetNumLocations() == 1,
                        VALID_BREAKPOINT)

        first_call_bpt = target.BreakpointCreateByLocation('pass-to-base.cpp', self.main_first_call_line)
        self.assertTrue(first_call_bpt.IsValid() and
                        first_call_bpt.GetNumLocations() == 1,
                        VALID_BREAKPOINT)

        second_call_bpt = target.BreakpointCreateByLocation('pass-to-base.cpp', self.main_second_call_line)
        self.assertTrue(second_call_bpt.IsValid() and
                        second_call_bpt.GetNumLocations() == 1,
                        VALID_BREAKPOINT)

        # Now launch the process, and do not stop at the entry point.
        self.process = target.LaunchSimple (None, None, os.getcwd())

        self.assertTrue(self.process.GetState() == lldb.eStateStopped,
                        PROCESS_STOPPED)

        threads = lldbutil.get_threads_stopped_at_breakpoint (self.process, first_call_bpt)
        self.assertTrue (len(threads) == 1)
        thread = threads[0]

        frame = thread.GetFrameAtIndex(0)

        # Now find the dynamic addresses of myB and otherB so we can compare them
        # with the dynamic values we get in doSomething:

        noDynamic = False
        useDynamic = True

        myB = frame.FindVariable ('myB', noDynamic);
        self.assertTrue (myB.IsValid())
        myB_loc = int (myB.GetLocation(), 16)

        otherB = frame.FindVariable('otherB', noDynamic)
        self.assertTrue (otherB.IsValid())
        otherB_loc = int (otherB.GetLocation(), 16)

        # Okay now run to doSomething:

        threads = lldbutil.continue_to_breakpoint (self.process, do_something_bpt)
        self.assertTrue (len(threads) == 1)
        thread = threads[0]

        frame = thread.GetFrameAtIndex(0)

        # Get "this" using FindVariable:

        this_static = frame.FindVariable ('this', noDynamic)
        this_dynamic = frame.FindVariable ('this', useDynamic)
        self.examine_value_object_of_this_ptr (this_static, this_dynamic, myB_loc)
        
        # Get "this" using FindValue, make sure that works too:
        this_static = frame.FindValue ('this', lldb.eValueTypeVariableArgument, noDynamic)
        this_dynamic = frame.FindValue ('this', lldb.eValueTypeVariableArgument, useDynamic)
        self.examine_value_object_of_this_ptr (this_static, this_dynamic, myB_loc)

        # Get "this" using the EvaluateExpression:
        # These tests fail for now because EvaluateExpression doesn't currently support dynamic typing...
        #this_static = frame.EvaluateExpression ('this', False)
        #this_dynamic = frame.EvaluateExpression ('this', True)
        #self.examine_value_object_of_this_ptr (this_static, this_dynamic, myB_loc)
        
        # The "frame var" code uses another path to get into children, so let's
        # make sure that works as well:

        self.expect('frame var -d 1 anotherA.m_client_A._M_ptr', 'frame var finds its way into a child member',
            patterns = ['\(.* B \*\)'])

        # Now make sure we also get it right for a reference as well:

        anotherA_static = frame.FindVariable ('anotherA', False)
        self.assertTrue (anotherA_static.IsValid())
        anotherA_static_addr = int (anotherA_static.GetValue(), 16)

        anotherA_dynamic = frame.FindVariable ('anotherA', True)
        self.assertTrue (anotherA_dynamic.IsValid())
        anotherA_dynamic_addr = int (anotherA_dynamic.GetValue(), 16)
        anotherA_dynamic_typename = anotherA_dynamic.GetTypeName()
        self.assertTrue (anotherA_dynamic_typename.find('B') != -1)

        self.assertTrue(anotherA_dynamic_addr < anotherA_static_addr)

        anotherA_m_b_value_dynamic = anotherA_dynamic.GetChildMemberWithName('m_b_value', True)
        self.assertTrue (anotherA_m_b_value_dynamic.IsValid())
        anotherA_m_b_val = int (anotherA_m_b_value_dynamic.GetValue(), 10)
        self.assertTrue (anotherA_m_b_val == 300)

        anotherA_m_b_value_static = anotherA_static.GetChildMemberWithName('m_b_value', True)
        self.assertTrue (anotherA_m_b_value_static.IsValid() == False)

        # Okay, now continue again, and when we hit the second breakpoint in main

        threads = lldbutil.continue_to_breakpoint (self.process, second_call_bpt)
        self.assertTrue (len(threads) == 1)
        thread = threads[0]

        frame = thread.GetFrameAtIndex(0)
        reallyA_value = frame.FindVariable ('reallyA', False)
        self.assertTrue(reallyA_value.IsValid())
        reallyA_loc = int (reallyA_value.GetLocation(), 16)
        
        # Finally continue to doSomething again, and make sure we get the right value for anotherA,
        # which this time around is just an "A".

        threads = lldbutil.continue_to_breakpoint (self.process, do_something_bpt)
        self.assertTrue(len(threads) == 1)
        thread = threads[0]

        frame = thread.GetFrameAtIndex(0)
        anotherA_value = frame.FindVariable ('anotherA', True)
        self.assertTrue(anotherA_value.IsValid())
        anotherA_loc = int (anotherA_value.GetValue(), 16)
        self.assertTrue (anotherA_loc == reallyA_loc)
        self.assertTrue (anotherA_value.GetTypeName().find ('B') == -1)

if __name__ == '__main__':
    import atexit
    lldb.SBDebugger.Initialize()
    atexit.register(lambda: lldb.SBDebugger.Terminate())
    unittest2.main()
