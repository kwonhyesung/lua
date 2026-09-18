return function(self, banInfo)
  ___MOD.log("[luahook] ClientAccountBanned type=" .. ___MOD.tostring(banInfo and banInfo.Type) .. " reason=" .. ___MOD.tostring(banInfo and banInfo.Reason))
  if banInfo == nil then
    return
  end
  local yOffset = 0
  local desc
  if banInfo.Type == 1 then
    if ___MOD._UtilLogic:IsNilorEmptyString(banInfo.Reason) then
      desc = "\236\157\188\236\139\156\236\160\129\236\156\188\235\161\156 \236\160\145\236\134\141\236\157\180 \236\176\168\235\139\168\235\144\156 \237\148\140\235\160\136\236\157\180\236\150\180\236\158\133\235\139\136\235\139\164.\n\236\158\160\236\139\156 \237\155\132 \235\139\164\236\139\156 \236\160\145\236\134\141\237\149\152\236\132\184\236\154\148."
    else
      desc = banInfo.Reason
    end
    yOffset = 50
  else
    local endTime
    if banInfo.EndAt == nil then
      endTime = ___MOD._MeramTimeStringFormattedType:GetTimeStringFormatted(___MOD._MeramTimeStringFormattedType.MaxValue)
    else
      local elapsedKST = banInfo.EndAt + ___MOD._DateTimeSpanUtils.NINE_HOURS_ELAPSED_VALUE
      endTime = ___MOD._DateTimeSpanUtils:GetStringFormatted(___MOD._MeramTimeStringFormattedType.yyyy_MM_dd_HH_mm_ss, elapsedKST)
    end
    desc = "\236\160\145\236\134\141 \236\160\156\236\158\172\235\144\156 \237\148\140\235\160\136\236\157\180\236\150\180\236\158\133\235\139\136\235\139\164.\n\236\130\172\236\156\160: " .. banInfo.Reason .. "\n\236\162\133\235\163\140 \236\139\156\234\176\132: " .. endTime
  end
  local dialog = ___MOD._MeramUIDialogService:ShowDialogNoDisrupt(desc, nil, nil, nil, nil)
  if dialog then
    dialog.Dialog.Entity.UITransformComponent.Position = ___MOD.Vector3(0, -350 + yOffset, 0)
  end
end
