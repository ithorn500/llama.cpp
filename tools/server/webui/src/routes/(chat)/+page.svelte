<script lang="ts">
	import { DialogModelNotAvailable } from '$lib/components/app';
	import { chatStore } from '$lib/stores/chat.svelte';
	import { conversationsStore, isConversationsInitialized } from '$lib/stores/conversations.svelte';
	import { modelsStore, modelOptions } from '$lib/stores/models.svelte';
	import { isRouterMode } from '$lib/stores/server.svelte';
	import { onMount } from 'svelte';
	import { page } from '$app/state';
	import { afterNavigate, replaceState } from '$app/navigation';
	import { APP_NAME } from '$lib/constants';

	let qParam = $derived(page.url.searchParams.get('q'));
	let modelParam = $derived(page.url.searchParams.get('model'));
	let newChatParam = $derived(page.url.searchParams.get('new_chat'));

	// Dialog state for model not available error
	let showModelNotAvailable = $state(false);
	let requestedModelName = $state('');
	let availableModelNames = $derived(modelOptions().map((m) => m.model));

	/**
	 * Clear URL params after message is sent to prevent re-sending on refresh
	 */
	function clearUrlParamsFrom(u: URL) {
		const url = new URL(u.href);
		url.searchParams.delete('q');
		url.searchParams.delete('model');
		url.searchParams.delete('new_chat');
		replaceState(url.toString(), {});
	}

	function clearUrlParams() {
		clearUrlParamsFrom(new URL(page.url.href));
	}

	/** True when hash router is on a real conversation, e.g. ``#/chat/<uuid>`` (not ``#/``). */
	function hashHasNamedChat(url: URL): boolean {
		const raw = url.hash.startsWith('#') ? url.hash.slice(1) : url.hash;
		return /^\/chat\/[^/?#]+/.test(raw);
	}

	async function handleUrlParams() {
		// ``modelsStore.fetch`` hits the network; skip when only ``new_chat`` (sidebar action).
		if (modelParam !== null || qParam !== null) {
			await modelsStore.fetch();
		}

		if (modelParam) {
			const model = modelsStore.findModelByName(modelParam);

			if (model) {
				try {
					await modelsStore.selectModelById(model.id);
				} catch (error) {
					console.error('Failed to select model:', error);
					requestedModelName = modelParam;
					showModelNotAvailable = true;

					return;
				}
			} else {
				requestedModelName = modelParam;
				showModelNotAvailable = true;

				return;
			}
		}

		// Handle ?q= parameter - create new conversation and send message
		if (qParam !== null) {
			await conversationsStore.createConversation();
			clearUrlParams();
		} else if (modelParam || newChatParam === 'true') {
			clearUrlParams();
		}
	}

	onMount(async () => {
		if (!isConversationsInitialized()) {
			await conversationsStore.initialize();
		}

		conversationsStore.clearActiveConversation();
		chatStore.clearUIState();

		if (
			isRouterMode() &&
			modelsStore.selectedModelName &&
			!modelsStore.isModelLoaded(modelsStore.selectedModelName)
		) {
			modelsStore.clearSelection();

			const first = modelOptions().find((m) => modelsStore.loadedModelIds.includes(m.model));
			if (first) {
				await modelsStore.selectModelById(first.id);
			}
		}

		// Handle URL params only if we have ?q= or ?model= or ?new_chat=true
		if (qParam !== null || modelParam !== null || newChatParam === 'true') {
			await handleUrlParams();
		}
	});

	// ``onMount`` does not run on client-side navigations. New chat uses
	// ``<a href="?new_chat=true#/">`` — handle repeat clicks here (skip ``enter``: first load uses ``onMount``).
	//
	// Important: after the first send, ``createConversation`` does ``goto('#/chat/<id>')`` but the query can
	// still be ``?new_chat=true``. Running a full reset then would clear ``activeConversation`` while
	// ``sendMessage`` / ``addMessage`` still need it — first message appears ignored; later turns break.
	afterNavigate(({ type, to }) => {
		if (type === 'enter') return;
		if (!to) return;
		const sp = to.url.searchParams;
		if (sp.get('new_chat') !== 'true' && sp.get('q') === null && sp.get('model') === null) {
			return;
		}
		if (!isConversationsInitialized()) return;

		if (sp.get('new_chat') === 'true' && hashHasNamedChat(to.url)) {
			clearUrlParamsFrom(to.url);
			return;
		}

		conversationsStore.clearActiveConversation();
		chatStore.clearUIState();

		if (
			isRouterMode() &&
			modelsStore.selectedModelName &&
			!modelsStore.isModelLoaded(modelsStore.selectedModelName)
		) {
			modelsStore.clearSelection();

			const first = modelOptions().find((m) => modelsStore.loadedModelIds.includes(m.model));
			if (first) {
				void modelsStore.selectModelById(first.id);
			}
		}

		void handleUrlParams();
	});
</script>

<svelte:head>
	<title>{APP_NAME}</title>
</svelte:head>

<DialogModelNotAvailable
	bind:open={showModelNotAvailable}
	modelName={requestedModelName}
	availableModels={availableModelNames}
/>
