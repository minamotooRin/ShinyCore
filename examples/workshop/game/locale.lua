local strings={
    zh={title='星灯工坊',move='移动  A/D    跳跃  SPACE',save='保存  E    读取进度  UP',room='房间 >'},
    en={title='STARLIGHT WORKSHOP',move='A/D MOVE   SPACE JUMP',save='E SAVE   UP LOAD',room='NEXT ROOM  ->'},
}
return function(language,key) return assert((strings[language] or strings.en)[key],key) end
