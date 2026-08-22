add_executable(appletinispeedtest
    main.cpp
    # NClock holds three ClockRails, each with an EventTimer.
    ../../src/util/EventTimer.cpp
)

add_test(NAME appletinispeedtest COMMAND appletinispeedtest)
