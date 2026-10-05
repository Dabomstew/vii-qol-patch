// New Game start bridge for the default VII native proxy.
// The proxy publishes a sequence counter and transition pointer in dinput8.dll
// data. This script reads both; it never writes game or proxy memory.
state("NeptuniaVII") { }

startup
{
    vars.ready = false;
    vars.sequenceSlot = IntPtr.Zero;
    vars.transitionSlot = IntPtr.Zero;
    vars.sequence = new MemoryWatcher<uint>(new DeepPointer(IntPtr.Zero));
    vars.transitionComplete = new MemoryWatcher<uint>(new DeepPointer(IntPtr.Zero, 0xD0));
}

init
{
    vars.ready = false;
    var proxy = modules.FirstOrDefault(x => x.ModuleName.ToLower() == "dinput8.dll");
    if (proxy != null)
    {
        var scanner = new SignatureScanner(game, proxy.BaseAddress, proxy.ModuleMemorySize);
        var marker = scanner.Scan(new SigScanTarget("56 49 49 37 4D 41 47 45"));
        if (marker != IntPtr.Zero)
        {
            vars.sequenceSlot = marker + 8;
            vars.transitionSlot = marker + 12;
            vars.sequence = new MemoryWatcher<uint>(new DeepPointer(vars.sequenceSlot));
            vars.transitionComplete = new MemoryWatcher<uint>(
                new DeepPointer(vars.transitionSlot, 0xD0));
            vars.sequence.Update(game);
            vars.transitionComplete.Update(game);
            vars.ready = true;
        }
    }
}

update
{
    if (!vars.ready)
        return false;
    vars.sequence.Update(game);
    vars.transitionComplete.Update(game);
    return true;
}

start
{
    return vars.ready && vars.sequence.Current != vars.sequence.Old;
}
