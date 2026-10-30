@startuml
participant Owner
participant Mutex
participant SharedMemory
participant Client1
participant Client2

Owner -> Mutex: WaitForSingleObject(INFINITE)
Owner -> SharedMemory: write state
Owner -> Mutex: ReleaseMutex

Client1 -> Mutex: WaitForSingleObject(timeout)
alt Timeout
    Client1 --> Client1: WAIT_TIMEOUT
else Success
    Client1 -> SharedMemory: validate data
    Client1 -> Mutex: ReleaseMutex
end

Client2 -> Mutex: WaitForSingleObject(timeout)
alt Abandoned
    Client2 --> Client2: WAIT_ABANDONED
else Success
    Client2 -> SharedMemory: validate and update
    Client2 -> Mutex: ReleaseMutex
end
@enduml
