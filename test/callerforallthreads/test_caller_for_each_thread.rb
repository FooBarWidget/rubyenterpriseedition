# -*- ruby-indent-level: 4 -*-
require 'thread'
require 'test/unit'

class AClassWithNestedmethods
  
  def an_ultra_nested_method(skip)
    caller_for_all_threads skip
  end

  def a_nested_method(skip)
    an_ultra_nested_method skip
  end

  def a_method(skip=0)
    a_nested_method skip
  end
  
end

class CallerForEachThreadTest < Test::Unit::TestCase
  
  def testCollectMeaningfulBacktraceForASingleThread
    backtraces = AClassWithNestedmethods.new.a_method
    backtrace = backtraces[Thread.current]
    assert_not_nil backtrace
    assert_equal __FILE__ + ":8:in `an_ultra_nested_method'", backtrace[0]
    assert_equal __FILE__ + ":12:in `a_nested_method'", backtrace[1]
    assert_equal __FILE__ + ":16:in `a_method'", backtrace[2]
    assert_equal __FILE__ + ":24:in `testCollectMeaningfulBacktraceForASingleThread'", 
                 backtrace[3]
  end

  def testCanSkipFirstStackEntries
    backtraces = AClassWithNestedmethods.new.a_method 2
    backtrace = backtraces[Thread.current]
    assert_not_nil backtrace
    assert_equal __FILE__ + ":16:in `a_method'", backtrace[0]
    assert_equal __FILE__ + ":35:in `testCanSkipFirstStackEntries'", 
                 backtrace[1]
  end

  def testCollectMeaningfulBacktraceForMultipleThreads
    first_thread = Thread.new do
      loop do
        Thread.pass
        sleep 1
      end
    end

    second_thread = Thread.new do
      loop do
        Thread.pass
        sleep 1
      end
    end

    backtraces = AClassWithNestedmethods.new.a_method
    
    backtrace = backtraces[Thread.current]
    assert_not_nil backtrace
    assert_match __FILE__ + ":8:in `an_ultra_nested_method'", backtrace[0]
    assert_match __FILE__ + ":12:in `a_nested_method'", backtrace[1]
    assert_equal __FILE__ + ":16:in `a_method'", backtrace[2]
    assert_equal __FILE__ + ":58:in `testCollectMeaningfulBacktraceForMultipleThreads'", 
                 backtrace[3]
                 
    backtrace = backtraces[first_thread]
    assert_not_nil backtrace
    assert_equal __FILE__ + ":47:in `testCollectMeaningfulBacktraceForMultipleThreads'", 
                 backtrace[0]
    assert_equal __FILE__ + ":45:in `loop'", 
                 backtrace[1]
    assert_equal __FILE__ + ":45:in `testCollectMeaningfulBacktraceForMultipleThreads'",
                 backtrace[2]
    assert_equal __FILE__ + ":44:in `initialize'",backtrace[3]
    assert_equal __FILE__ + ":44:in `new'", backtrace[4]
    assert_equal __FILE__ + ":44:in `testCollectMeaningfulBacktraceForMultipleThreads'",
                 backtrace[5]

    backtrace = backtraces[second_thread]
    assert_not_nil backtrace
    assert_equal __FILE__ + ":53:in `testCollectMeaningfulBacktraceForMultipleThreads'", 
                 backtrace[0]
    assert_equal __FILE__ + ":52:in `loop'", backtrace[1]
    assert_equal __FILE__ + ":52:in `testCollectMeaningfulBacktraceForMultipleThreads'",
                 backtrace[2]
    assert_equal __FILE__ + ":51:in `initialize'",backtrace[3]
    assert_equal __FILE__ + ":51:in `new'", backtrace[4]
    assert_equal __FILE__ + ":51:in `testCollectMeaningfulBacktraceForMultipleThreads'",
                 backtrace[5]
  end
  
end

