return function(self)
  ___MOD.log("[luahook] ClientOnDisconnect")
  ___MOD._MeramSoundService:PlayBGM(999, 1.0)
  ___MOD._MeramCreatureService:ProcessLeaveFromMap(nil, true)
end
