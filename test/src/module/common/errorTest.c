/***********************************************************************************************************************************
Test Error Handling
***********************************************************************************************************************************/
#include <assert.h>

#include "common/harnessFork.h"

/***********************************************************************************************************************************
Declare some error locally because real errors won't work for some tests -- they could also break as errors change
***********************************************************************************************************************************/
ERROR_DECLARE(TestParent1Error);
ERROR_DECLARE(TestParent2Error);
ERROR_DECLARE(TestChildError);

ERROR_DEFINE(101, TestParent1Error, false, TestParent1Error);
ERROR_DEFINE(102, TestParent2Error, false, TestParent1Error);
ERROR_DEFINE(200, TestChildError, false, TestParent2Error);

/***********************************************************************************************************************************
testTryRecurse - test to blow up try stack
***********************************************************************************************************************************/
static volatile int testTryRecurseTotal = 0;
static bool testTryRecurseCatch = false;
static bool testTryRecurseFinally = false;

static void
testTryRecurse(void)
{
    TRY_BEGIN()
    {
        testTryRecurseTotal++;
        assert(errorContext.tryTotal == testTryRecurseTotal + 1);

        testTryRecurse();
    }
    CATCH(UnhandledError)
    {
        testTryRecurseCatch = true;                                 // {uncoverable - catch should never be executed}
    }
    FINALLY()
    {
        testTryRecurseFinally = true;
    }
    TRY_END();
}                                                                   // {uncoverable - function throws error, never returns}

/***********************************************************************************************************************************
Test error handler
***********************************************************************************************************************************/
static unsigned int testErrorHandlerTryDepth;
static bool testErrorHandlerFatal;

static void
testErrorHandler(unsigned int tryDepth, bool fatal)
{
    testErrorHandlerFatal = fatal;
    testErrorHandlerTryDepth = tryDepth;
}

/***********************************************************************************************************************************
Test Run
***********************************************************************************************************************************/
static void
testRun(void)
{
    FUNCTION_HARNESS_VOID();

    // *****************************************************************************************************************************
    if (testBegin("check that try stack is initialized correctly"))
    {
        assert(errorContext.tryTotal == 0);
    }

    // *****************************************************************************************************************************
    if (testBegin("errorTypeExtends"))
    {
        assert(errorTypeExtends(&TestParent1Error, &TestParent1Error));
        assert(errorTypeExtends(&TestChildError, &TestParent1Error));
        assert(errorTypeExtends(&TestChildError, &TestParent2Error));
        assert(!errorTypeExtends(&TestChildError, &TestChildError));
    }

    // *****************************************************************************************************************************
    if (testBegin("TRY with no errors"))
    {
        volatile bool tryDone = false;
        bool catchDone = false;
        bool finallyDone = false;

        TRY_BEGIN()
        {
            assert(errorContext.tryTotal == 1);
            tryDone = true;
        }
        CATCH_ANY()
        {
            catchDone = true;                                       // {uncoverable - catch should never be executed}
        }
        FINALLY()
        {
            assert(errorContext.tryList[1].state == errorStateTry);
            finallyDone = true;
        }
        TRY_END();

        assert(tryDone);
        assert(!catchDone);
        assert(finallyDone);
        assert(errorContext.tryTotal == 0);
    }

    // *****************************************************************************************************************************
    if (testBegin("errorInternalThrow()"))
    {
        TEST_TITLE("specify all parameters");

        TRY_BEGIN()
        {
            errorInternalThrow(&FormatError, "file77", "function88", 99, "message100", "stacktrace200");
        }
        CATCH_ANY()
        {
            assert(errorType() == &FormatError);
        }
        TRY_END();
    }

    // *****************************************************************************************************************************
    if (testBegin("TRY with multiple catches"))
    {
        volatile bool tryDone = false;
        volatile bool catchDone = false;
        volatile bool finallyDone = false;

        // -------------------------------------------------------------------------------------------------------------------------
        TEST_TITLE("set error handler");

        static const ErrorHandlerFunction testErrorHandlerList[] = {testErrorHandler};
        errorHandlerSet(testErrorHandlerList, LENGTH_OF(testErrorHandlerList));

        assert(errorContext.handlerList[0] == testErrorHandler);
        assert(errorContext.handlerTotal == 1);

        // -------------------------------------------------------------------------------------------------------------------------
        assert(errorTryDepth() == 0);

        TRY_BEGIN()
        {
            assert(errorTryDepth() == 1);

            TRY_BEGIN()
            {
                assert(errorTryDepth() == 2);

                TRY_BEGIN()
                {
                    assert(errorTryDepth() == 3);

                    TRY_BEGIN()
                    {
                        assert(errorTryDepth() == 4);

                        TRY_BEGIN()
                        {
                            assert(errorTryDepth() == 5);
                            tryDone = true;
                        }
                        FINALLY()
                        {
                            assert(errorContext.tryList[5].state == errorStateTry);

                            char bigMessage[sizeof(messageBuffer) + 128];
                            memset(bigMessage, 'A', sizeof(bigMessage));

                            THROW(AssertError, bigMessage);
                        }
                        TRY_END();
                    }
                    CATCH_ANY()
                    {
                        // Catch should not be executed since this error is fatal
                        assert(false);
                    }
                    TRY_END();
                }
                CATCH_FATAL()
                {
                    assert(testErrorHandlerTryDepth == 3);
                    assert(testErrorHandlerFatal);

                    // Change to FormatError so error can be caught by normal catches
                    THROW(FormatError, errorMessage());
                }
                // Should run even though an error has been thrown in the catch
                FINALLY()
                {
                    finallyDone = true;
                }
                TRY_END();
            }
            CATCH_ANY()
            {
                assert(testErrorHandlerTryDepth == 2);
                assert(!testErrorHandlerFatal);

                RETHROW();
            }
            TRY_END();
        }
        CATCH(UnhandledError)
        {
            assert(false);                                              // {uncoverable - catch should never be executed}
        }
        CATCH(RuntimeError)
        {
            assert(testErrorHandlerTryDepth == 1);
            assert(!testErrorHandlerFatal);
            assert(errorTryDepth() == 1);
            assert(errorContext.tryList[1].state == errorStateEnd);
            assert(strlen(errorMessage()) == sizeof(messageBuffer) - 1);

            catchDone = true;
        }
        TRY_END();

        assert(errorTryDepth() == 0);
        assert(memcmp(&errorContext.error, &(Error){0}, sizeof(Error)) == 0);

        assert(tryDone);
        assert(catchDone);
        assert(finallyDone);
        assert(errorContext.tryTotal == 0);
    }

    // *****************************************************************************************************************************
    if (testBegin("too deep recursive TRY_ERROR()"))
    {
        volatile bool tryDone = false;
        bool catchDone = false;
        bool finallyDone = false;

        TRY_BEGIN()
        {
            tryDone = true;
            testTryRecurse();
        }
        CATCH_FATAL()
        {
            assert(errorCode() == AssertError.code);
            assert(strcmp(errorFileName(), TEST_PGB_PATH "/test/src/module/common/errorTest.c") == 0);
            assert(strcmp(errorFunctionName(), "testTryRecurse") == 0);
            assert(errorFileLine() == 29);
            assert(errorStackTrace() != NULL);
            assert(strcmp(errorMessage(), "too many nested try blocks") == 0);
            assert(strcmp(errorName(), AssertError.name) == 0);
            assert(errorType() == &AssertError);
            assert(errorTypeCode(errorType()) == AssertError.code);
            assert(strcmp(errorTypeName(errorType()), AssertError.name) == 0);

            catchDone = true;
        }
        FINALLY()
        {
            finallyDone = true;
        }
        TRY_END();

        assert(memcmp(&errorContext.error, &(Error){0}, sizeof(Error)) == 0);

        assert(tryDone);
        assert(catchDone);
        assert(finallyDone);
        assert(errorContext.tryTotal == 0);

        // This is only ERROR_TRY_MAX - 1 because one try was used up by the wrapper above the recursive function
        assert(testTryRecurseTotal == ERROR_TRY_MAX - 1);
        assert(!testTryRecurseCatch);
        assert(testTryRecurseFinally);
    }

    // *****************************************************************************************************************************
    if (testBegin("THROW_CODE() and THROW_CODE_FMT()"))
    {
        TRY_BEGIN()
        {
            THROW_CODE(25, "message");
        }
        CATCH_FATAL()
        {
            assert(errorCode() == 25);
            assert(strcmp(errorMessage(), "message") == 0);
        }
        TRY_END();

        // -------------------------------------------------------------------------------------------------------------------------
        TRY_BEGIN()
        {
            THROW_CODE_FMT(122, "message %d", 1);
        }
        CATCH_ANY()
        {
            assert(errorCode() == 122);
            assert(strcmp(errorMessage(), "message 1") == 0);
        }
        TRY_END();

        // -------------------------------------------------------------------------------------------------------------------------
        TRY_BEGIN()
        {
            THROW_CODE(777, "message");
        }
        CATCH_ANY()
        {
            assert(errorCode() == UnknownError.code);
            assert(strcmp(errorMessage(), "message") == 0);
        }
        TRY_END();
    }

    // *****************************************************************************************************************************
    if (testBegin("THROW_SYS_ERROR() and THROW_SYS_ERROR_FMT()"))
    {
        THROW_ON_SYS_ERROR(false, AssertError, "no error");
        THROW_ON_SYS_ERROR_FMT(false, AssertError, "no error");

        TRY_BEGIN()
        {
            errno = E2BIG;
            THROW_ON_SYS_ERROR(true, AssertError, "message");
        }
        CATCH_FATAL()
        {
            printf("%s\n", errorMessage());
            assert(errorCode() == AssertError.code);
            assert(strcmp(errorMessage(), "message: [7] Argument list too long") == 0);
        }
        TRY_END();

        // -------------------------------------------------------------------------------------------------------------------------
        TRY_BEGIN()
        {
            errno = 0;
            THROW_ON_SYS_ERROR_FMT(true, AssertError, "message %d", 77);
        }
        CATCH_FATAL()
        {
            printf("%s\n", errorMessage());
            assert(errorCode() == AssertError.code);
            assert(strcmp(errorMessage(), "message 77") == 0);
        }
        TRY_END();

        // -------------------------------------------------------------------------------------------------------------------------
        TRY_BEGIN()
        {
            errno = E2BIG;
            THROW_ON_SYS_ERROR_FMT(true, AssertError, "message %d", 77);
        }
        CATCH_FATAL()
        {
            printf("%s\n", errorMessage());
            assert(errorCode() == AssertError.code);
            assert(strcmp(errorMessage(), "message 77: [7] Argument list too long") == 0);
        }
        TRY_END();

        // -------------------------------------------------------------------------------------------------------------------------
        TRY_BEGIN()
        {
            errno = 0;
            THROW_SYS_ERROR(AssertError, "message");
        }
        CATCH_FATAL()
        {
            printf("%s\n", errorMessage());
            assert(errorCode() == AssertError.code);
            assert(strcmp(errorMessage(), "message") == 0);
        }
        TRY_END();

        // -------------------------------------------------------------------------------------------------------------------------
        TRY_BEGIN()
        {
            errno = EIO;
            THROW_SYS_ERROR_FMT(AssertError, "message %d", 1);
        }
        CATCH_FATAL()
        {
            printf("%s\n", errorMessage());
            assert(errorCode() == AssertError.code);
            assert(
                // glibc
                strcmp(errorMessage(), "message 1: [5] Input/output error") == 0 ||
                // musl libc
                strcmp(errorMessage(), "message 1: [5] I/O error") == 0);
        }
        TRY_END();

        // -------------------------------------------------------------------------------------------------------------------------
        TRY_BEGIN()
        {
            errno = 0;
            THROW_SYS_ERROR_FMT(AssertError, "message %d", 1);
        }
        CATCH_FATAL()
        {
            printf("%s\n", errorMessage());
            assert(errorCode() == AssertError.code);
            assert(strcmp(errorMessage(), "message 1") == 0);
        }
        TRY_END();
    }

    // *****************************************************************************************************************************
    if (testBegin("Uncaught error"))
    {
        // Test in a fork so the process does not actually exit
        HRN_FORK_BEGIN()
        {
            HRN_FORK_CHILD_BEGIN(.expectedExitStatus = UnhandledError.code)
            {
                // Redirect stderr to stdout (we do not care about the output here since coverage will tell us we hit the code)
                dup2(fileno(stdout), fileno(stderr));

                THROW(TestChildError, "does not get caught!");
            }
            HRN_FORK_CHILD_END();
        }
        HRN_FORK_END();
    }

    FUNCTION_HARNESS_RETURN_VOID();
}
