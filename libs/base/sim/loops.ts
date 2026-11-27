namespace pxsim.loops {
    export let pause = thread.pause;
    export let forever = thread.forever;
    export function onStartOnCore(_core: number, a: RefAction) {
        thread.runInBackground(a);
    }
    export function foreverOnCore(_core: number, a: RefAction) {
        thread.forever(a);
    }
}