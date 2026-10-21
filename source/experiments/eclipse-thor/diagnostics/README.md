# Archived Eclipse startup stack observer

`ThorUiWatchdog.java` is an observational template retained from the ANR investigation.
It is not part of the final host's source set, and no invocation remains in GameActivity.
For an explicitly requested future diagnostic build, copy it to the host's
`dev/chasem/smslauncher` source package and call it from onCreate behind
`BuildConfig.DEBUG`. It samples Java thread states/stacks at 2/5/10/20/30 seconds;
it does not change gameplay, terminate a process or access saves. Remove it again after
collecting the required evidence.
