<script lang="ts">
	import { browser } from '$app/environment';
	import { base } from '$app/paths';
	import { onMount } from 'svelte';
	import { config } from '$lib/stores/settings.svelte';
	import { getAuthHeaders } from '$lib/utils/api-headers';

	interface Props {
		class?: string;
		show?: boolean;
	}

	let { class: className = '', show = true }: Props = $props();

	let sendOnEnter = $derived(config().sendOnEnter !== false);
	let modKey = browser && /Mac|iPhone|iPad|iPod/.test(navigator.platform) ? 'Cmd' : 'Ctrl';
	let queueWaiting = $state<number | null>(null);
	let queueInFlight = $state<number | null>(null);

	let queueAhead = $derived.by(() => {
		if (queueWaiting === null || queueInFlight === null) return null;
		return Math.max(0, queueWaiting + queueInFlight);
	});

	async function refreshQueueLength() {
		try {
			const response = await fetch(`${base}/api/v1/ops/activity?veliai_mode=slim`, {
				headers: getAuthHeaders()
			});
			if (!response.ok) return;

			const payload = await response.json();
			const queue = payload?.local_llm_queue;
			if (!queue || typeof queue !== 'object') return;

			const waiting = Number(queue.waiting);
			const inFlight = Number(queue.in_flight);
			if (Number.isFinite(waiting) && Number.isFinite(inFlight)) {
				queueWaiting = Math.max(0, Math.trunc(waiting));
				queueInFlight = Math.max(0, Math.trunc(inFlight));
			}
		} catch {
			// Optional UI metric only.
		}
	}

	onMount(() => {
		if (!browser) return;
		refreshQueueLength();
		const timer = setInterval(refreshQueueLength, 3000);
		return () => clearInterval(timer);
	});
</script>

{#if show}
	<div class="mt-6 items-center justify-center gap-3 {className} hidden md:flex">
		{#if sendOnEnter}
			<p class="text-xs text-muted-foreground">
				Press <kbd class="rounded bg-muted px-1 py-0.5 font-mono text-xs">Enter</kbd> to send,
				<kbd class="rounded bg-muted px-1 py-0.5 font-mono text-xs">Shift + Enter</kbd> for new line
			</p>
		{:else}
			<p class="text-xs text-muted-foreground">
				Press <kbd class="rounded bg-muted px-1 py-0.5 font-mono text-xs">{modKey} + Enter</kbd> to
				send,
				<kbd class="rounded bg-muted px-1 py-0.5 font-mono text-xs">Enter</kbd> for new line
			</p>
		{/if}

		{#if queueAhead !== null}
			<p class="text-xs text-muted-foreground">
				Queue: <span class="font-medium text-foreground">{queueAhead}</span>
				({queueWaiting} waiting, {queueInFlight} in-flight)
			</p>
		{/if}
	</div>
{/if}
