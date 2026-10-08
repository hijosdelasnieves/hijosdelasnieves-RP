Scriptname HdnTailorStudio

Int Function ApiVersion() Global Native
Int Function BeginSession() Global Native
Bool Function Snapshot(Int token, Int revision, Int actorId) Global Native
Bool Function Frame(Int token, Float yaw, Float zoom) Global Native
Int Function GetStatus(Int token) Global Native
Bool Function EndSession(Int token) Global Native
